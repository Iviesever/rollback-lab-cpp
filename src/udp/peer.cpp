#include <rollback_lab/udp/peer.hpp>

#include <rollback_lab/netcode/session.hpp>
#include <rollback_lab/core/hash.hpp>
#include <rollback_lab/protocol/bytes.hpp>
#include <rollback_lab/protocol/codec.hpp>
#include <rollback_lab/protocol/crc32.hpp>
#include <rollback_lab/protocol/sequence_window.hpp>
#include <rollback_lab/replay/replay.hpp>
#include <rollback_lab/report/canonical_json.hpp>
#include <rollback_lab/report/run_report.hpp>
#include <rollback_lab/simulation/scripted_input.hpp>
#include <rollback_lab/transport/udp_socket.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <sstream>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace rollback_lab {
namespace {

struct PeerRuntime final {
    RollbackSession& session;
    SequenceWindow sequences;
    DesyncTracker desync;
    std::vector<InputFrame> local_history;
    std::optional<StateHash> remote_final_hash;
    TransportMetrics metrics{};
    std::uint32_t next_sequence{2U};
    PeerCorrection correction{};
    std::optional<DesyncDiagnostic> diagnostic;
};

auto remote_id(const PlayerId local) -> PlayerId {
    return local == PlayerId::a ? PlayerId::b : PlayerId::a;
}

auto scenario_config_digest(const PeerConfig& config) -> StateHash {
    ByteWriter bytes;
    if (config.engine_abi_version != 0U) {
        constexpr std::string_view profile{"RollbackLab.engine-udp-v1"};
        for (const char byte : profile) bytes.append_little_endian(static_cast<std::uint8_t>(byte));
        bytes.append_little_endian(config.advertised_abi_version != 0U
            ? config.advertised_abi_version : config.engine_abi_version);
        bytes.append_little_endian(config.protocol_version_override);
        bytes.append_little_endian(config.transport_seed);
    }
    bytes.append_little_endian(config.scenario_seed);
    bytes.append_little_endian(config.frame_count);
    bytes.append_little_endian(config.simulation_version_override);
    bytes.append_little_endian(kProtocolVersion);
    return fnv1a64(bytes.bytes());
}

auto valid_hello(const PeerConfig& config, const Packet& packet)
    -> Result<void> {
    if (packet.type != PacketType::hello ||
        packet.sender != remote_id(config.id) ||
        packet.scenario_id != config.scenario_seed || !packet.hello.has_value()) {
        return Result<void>::failure(
            Error{ErrorCode::invalid_argument, packet.scenario_id, 0U,
                  "udp_hello_identity"});
    }
    if (packet.hello->simulation_version != kSimulationVersion) {
        return Result<void>::failure(
            Error{ErrorCode::unsupported_version,
                  packet.hello->simulation_version, 0U,
                  "udp_simulation_version"});
    }
    PeerConfig expected = config;
    expected.simulation_version_override = kSimulationVersion;
    expected.protocol_version_override = static_cast<std::uint16_t>(kProtocolVersion);
    expected.advertised_abi_version = expected.engine_abi_version;
    if (packet.hello->target_frame != config.frame_count ||
        packet.hello->scenario_config_digest !=
            scenario_config_digest(expected)) {
        return Result<void>::failure(
            Error{ErrorCode::invalid_argument,
                  packet.hello->scenario_config_digest, 0U,
                  config.engine_abi_version != 0U ? "udp_engine_profile" : "udp_scenario_config"});
    }
    return Result<void>::success();
}

auto encode_hello(const PeerConfig& config) -> Result<std::vector<std::byte>> {
    Packet hello{};
    hello.type = PacketType::hello;
    hello.sender = config.id;
    hello.sequence = 1U;
    hello.scenario_id = config.scenario_seed;
    hello.hello = HelloInfo{config.simulation_version_override,
                            scenario_config_digest(config),
                            config.frame_count};
    const auto encoded = encode_packet(hello);
    if (!encoded.ok()) {
        return encoded;
    }
    auto bytes = encoded.value();
    if (config.protocol_version_override != kProtocolVersion) {
        bytes[4] = static_cast<std::byte>(config.protocol_version_override & 0xFFU);
        bytes[5] = static_cast<std::byte>(
            (config.protocol_version_override >> 8U) & 0xFFU);
        const auto checksum = crc32(
            std::span<const std::byte>{bytes}.first(bytes.size() - 4U));
        for (std::size_t index = 0U; index < 4U; ++index) {
            bytes[bytes.size() - 4U + index] = static_cast<std::byte>(
                (checksum >> static_cast<unsigned>(index * 8U)) & 0xFFU);
        }
    }
    return Result<std::vector<std::byte>>::success(std::move(bytes));
}

auto input_window(const std::vector<InputFrame>& history)
    -> std::vector<InputFrame> {
    const auto count = std::min(history.size(), kMaxRedundantInputs);
    return std::vector<InputFrame>{
        history.end() - static_cast<std::ptrdiff_t>(count), history.end()};
}

auto confirmed_hash_window(const RollbackSession& session)
    -> std::vector<ConfirmedHash> {
    const auto confirmed = session.report().metrics.confirmed_frame;
    std::vector<ConfirmedHash> hashes;
    hashes.reserve(kMaxConfirmedHashes);
    for (std::uint32_t offset = 0U; offset < kMaxConfirmedHashes; ++offset) {
        const auto frame = FrameNumber{
            static_cast<std::uint32_t>(confirmed.value - offset)};
        const auto hash = session.hash_at(frame);
        if (!hash.ok()) {
            break;
        }
        hashes.push_back(ConfirmedHash{frame, hash.value()});
    }
    std::reverse(hashes.begin(), hashes.end());
    return hashes;
}

auto diagnostic_inputs(const RollbackSession& session,
                       const FrameNumber boundary) -> std::vector<InputPair> {
    std::vector<InputPair> inputs;
    inputs.reserve(32U);
    for (std::uint32_t offset = 1U; offset <= 32U; ++offset) {
        const auto frame = FrameNumber{
            static_cast<std::uint32_t>(boundary.value - offset)};
        const auto input_a = session.confirmed_input(PlayerId::a, frame);
        const auto input_b = session.confirmed_input(PlayerId::b, frame);
        if (!input_a.ok() || !input_b.ok()) {
            break;
        }
        inputs.push_back(InputPair{input_a.value(), input_b.value()});
    }
    std::reverse(inputs.begin(), inputs.end());
    return inputs;
}

auto send_inputs(const PeerConfig& config,
                 UdpSocket& socket,
                 PeerRuntime& runtime) -> Result<void> {
    const auto report = runtime.session.report();
    Packet packet{};
    packet.type = PacketType::input;
    packet.sender = config.id;
    packet.sequence = runtime.next_sequence++;
    packet.ack = report.metrics.confirmed_frame.value;
    packet.scenario_id = config.scenario_seed;
    packet.confirmed_frame = report.metrics.confirmed_frame;
    packet.inputs = input_window(runtime.local_history);
    packet.confirmed_hashes = confirmed_hash_window(runtime.session);
    const auto encoded = encode_packet(packet);
    if (!encoded.ok()) {
        return Result<void>::failure(encoded.error());
    }
    ++runtime.metrics.sent_packets;
    return socket.send_loopback(config.relay_port, encoded.value());
}

auto receive_one(const PeerConfig& config,
                 UdpSocket& socket,
                 PeerRuntime& runtime,
                 const std::chrono::milliseconds timeout) -> Result<bool> {
    const auto received = socket.receive_for(timeout);
    if (!received.ok()) {
        return Result<bool>::failure(received.error());
    }
    if (!received.value().has_value()) {
        return Result<bool>::success(false);
    }
    if (config.engine_abi_version != 0U && received.value()->source_port != config.relay_port) {
        return Result<bool>::failure(Error{ErrorCode::invalid_argument,
            received.value()->source_port, 0U, "udp_relay_source"});
    }
    const auto decoded = decode_packet(received.value()->bytes);
    if (!decoded.ok()) {
        return Result<bool>::failure(decoded.error());
    }
    const auto& packet = decoded.value();
    if (packet.sender != remote_id(config.id) ||
        packet.scenario_id != config.scenario_seed) {
        return Result<bool>::failure(
            Error{ErrorCode::invalid_argument, packet.scenario_id, 0U,
                  "peer_packet_identity"});
    }
    if (packet.type == PacketType::hello) {
        const auto validated = valid_hello(config, packet);
        return validated.ok() ? Result<bool>::success(true)
                              : Result<bool>::failure(validated.error());
    }
    if (packet.type != PacketType::input) {
        return Result<bool>::failure(
            Error{ErrorCode::unknown_packet_type,
                  static_cast<std::uint64_t>(packet.type), 0U,
                  "peer_packet_type"});
    }
    const auto disposition = runtime.sequences.observe(packet.sequence);
    if (disposition == SequenceDisposition::duplicate ||
        disposition == SequenceDisposition::stale) {
        return Result<bool>::success(true);
    }
    ++runtime.metrics.delivered_packets;
    if (disposition == SequenceDisposition::out_of_order) {
        ++runtime.metrics.reordered_packets;
    }
    for (const auto& input : packet.inputs) {
        if (config.engine_abi_version != 0U && input.frame.value >= config.frame_count) {
            return Result<bool>::failure(Error{ErrorCode::frame_mismatch,
                input.frame.value, 0U, "udp_input_target"});
        }
    }
    const auto before = runtime.session.report().state;
    for (const auto& input : packet.inputs) {
        const auto ingested = runtime.session.ingest_remote(input);
        if (!ingested.ok()) {
            return Result<bool>::failure(ingested.error());
        }
    }
    const auto corrected = runtime.session.flush_corrections();
    if (!corrected.ok()) {
        return Result<bool>::failure(corrected.error());
    }
    if (corrected.value().performed) {
        runtime.correction = PeerCorrection{corrected.value(), before, runtime.session.report().state};
    }
    for (const auto& remote_hash : packet.confirmed_hashes) {
        const auto local = runtime.session.hash_at(remote_hash.frame);
        if (local.ok()) {
            if (local.value() != remote_hash.hash) {
                const auto local_state =
                    runtime.session.state_at(remote_hash.frame);
                if (!local_state.ok()) {
                    return Result<bool>::failure(local_state.error());
                }
                const auto diagnostic = runtime.desync.observe(
                    HashObservation{remote_hash.frame, local.value(), true},
                    HashObservation{remote_hash.frame, remote_hash.hash, true},
                    diagnostic_inputs(runtime.session, remote_hash.frame),
                    local_state.value());
                if (!diagnostic.has_value()) {
                    return Result<bool>::failure(
                        Error{ErrorCode::desync, remote_hash.hash,
                              remote_hash.frame.value,
                              "udp_desync_missing_diagnostic"});
                }
                runtime.diagnostic = diagnostic;
                return Result<bool>::failure(
                    Error{ErrorCode::desync, remote_hash.hash,
                          remote_hash.frame.value,
                          "udp_confirmed_hash"});
            }
            if (remote_hash.frame == FrameNumber{config.frame_count}) {
                runtime.remote_final_hash = remote_hash.hash;
            }
        }
    }
    return Result<bool>::success(true);
}

auto drain_available(const PeerConfig& config,
                     UdpSocket& socket,
                     PeerRuntime& runtime) -> Result<void> {
    for (std::uint32_t packet = 0U; packet < 64U; ++packet) {
        const auto received = receive_one(config, socket, runtime,
                                          std::chrono::milliseconds{0});
        if (!received.ok()) {
            return Result<void>::failure(received.error());
        }
        if (!received.value()) {
            break;
        }
    }
    return Result<void>::success();
}

auto build_replay(const PeerConfig& config, const RollbackSession& session)
    -> Result<Replay> {
    Replay replay{};
    replay.scenario_seed = config.scenario_seed;
    replay.transport_seed = config.transport_seed;
    replay.final_frame = FrameNumber{config.frame_count};
    auto state = make_initial_world();
    replay.confirmed_inputs.reserve(config.frame_count);
    for (std::uint32_t frame = 0U; frame < config.frame_count; ++frame) {
        const auto number = FrameNumber{frame};
        const auto input_a = session.confirmed_input(PlayerId::a, number);
        const auto input_b = session.confirmed_input(PlayerId::b, number);
        if (!input_a.ok() || !input_b.ok()) {
            return Result<Replay>::failure(
                Error{ErrorCode::stale_frame, frame, 0U, "udp_replay_input"});
        }
        const InputPair pair{input_a.value(), input_b.value()};
        replay.confirmed_inputs.push_back(pair);
        const auto next = simulate_frame(state, number, pair);
        if (!next.ok()) {
            return Result<Replay>::failure(next.error());
        }
        state = next.value();
        if (state.frame.value % 30U == 0U ||
            state.frame.value == config.frame_count) {
            replay.checkpoints.push_back(
                ReplayCheckpoint{state.frame, hash_canonical(state)});
        }
    }
    replay.expected_final_hash = hash_canonical(state);
    return Result<Replay>::success(std::move(replay));
}

auto engine_identity_fields(const PeerConfig& config) -> std::string {
    std::ostringstream out;
    out << "\"udp_profile\":\"engine-udp-v1\",\"abi_version\":" << config.engine_abi_version
        << ",\"local_peer\":" << static_cast<unsigned>(config.id)
        << ",\"listen_port\":" << config.listen_port
        << ",\"relay_port\":" << config.relay_port
        << ",\"advertised_protocol_version\":" << config.protocol_version_override
        << ",\"advertised_simulation_version\":" << config.simulation_version_override
        << ",\"advertised_abi_profile_version\":" << (config.advertised_abi_version
            ? config.advertised_abi_version : config.engine_abi_version)
        << ",\"advertised_config_digest\":" << scenario_config_digest(config);
    return out.str();
}
struct PeerArtifacts final { std::string report; std::vector<std::byte> replay; };

auto build_peer_artifacts(const PeerConfig& config,
                          const PeerRuntime& runtime) -> Result<PeerArtifacts> {
    const auto replay = build_replay(config, runtime.session);
    if (!replay.ok()) {
        return Result<PeerArtifacts>::failure(replay.error());
    }
    const auto verified = verify_replay(replay.value());
    const auto encoded = encode_replay(replay.value());
    if (!verified.ok() || !encoded.ok()) {
        return Result<PeerArtifacts>::failure(
            !verified.ok() ? verified.error() : encoded.error());
    }
    const auto session = runtime.session.report();
    if (config.engine_abi_version != 0U &&
        session.final_hash != replay.value().expected_final_hash) {
        return Result<PeerArtifacts>::failure(Error{ErrorCode::replay_mismatch,
            session.final_hash, config.frame_count, "udp_canonical_replay"});
    }
    RunReport report{};
    report.build_type =
#if defined(NDEBUG)
        "Release";
#else
        "Debug";
#endif
    report.compiler =
#if defined(__clang__)
        "Clang";
#elif defined(_MSC_VER)
        "MSVC";
#else
        "GCC";
#endif
    report.os =
#if defined(_WIN32)
        "Windows";
#else
        "POSIX";
#endif
    report.scenario_seed = config.scenario_seed;
    report.transport_seed = config.transport_seed;
    report.frame_count = config.frame_count;
    report.transport_metrics = runtime.metrics;
    report.rollback_count = session.metrics.rollback_count;
    report.resimulated_frames = session.metrics.total_resimulated_frames;
    report.maximum_rollback_depth = session.metrics.maximum_rollback_depth;
    report.predicted_input_count = session.metrics.predicted_input_count;
    report.late_input_count = session.metrics.late_input_count;
    report.confirmed_frame = session.metrics.confirmed_frame;
    if (config.id == PlayerId::a) {
        report.final_hash_a = session.final_hash;
        report.final_hash_b = runtime.remote_final_hash.value_or(0U);
    } else {
        report.final_hash_a = runtime.remote_final_hash.value_or(0U);
        report.final_hash_b = session.final_hash;
    }
    report.replay_verified = true;
    report.desync_result = "none";
    report.success = runtime.remote_final_hash.has_value() &&
                     report.final_hash_a == report.final_hash_b &&
                     report.confirmed_frame == FrameNumber{config.frame_count};
    if (!report.success) {
        report.failure_reason = "udp peer did not confirm final remote hash";
    }
    const auto json = canonical_json(report);
    if (!json.ok()) {
        return Result<PeerArtifacts>::failure(json.error());
    }
    if (!report.success) {
        return Result<PeerArtifacts>::failure(Error{ErrorCode::replay_mismatch,
            session.final_hash, config.frame_count, "udp_peer_artifacts"});
    }
    const auto text = config.engine_abi_version
        ? "{" + engine_identity_fields(config) + "," + json.value().substr(1) : json.value();
    return Result<PeerArtifacts>::success(PeerArtifacts{text, encoded.value()});
}

}  // namespace

struct PeerDriver::Impl final {
    PeerConfig config;
    UdpSocket socket;
    PeerRuntime runtime;
    std::vector<std::byte> hello;
    PeerStep observation{};
    std::optional<Error> failure;
    std::uint64_t last_elapsed{};
    std::uint64_t run_started{};
    std::uint32_t final_drain_progress{};
    PeerArtifacts artifacts;

    Impl(PeerConfig c, UdpSocket s, RollbackSession& session, std::vector<std::byte> h)
        : config(std::move(c)), socket(std::move(s)),
          runtime{session, SequenceWindow{}, DesyncTracker{config.scenario_seed},
                  {}, std::nullopt, TransportMetrics{}, 2U, {}, std::nullopt}, hello(std::move(h)) {
        config.listen_port = socket.local_port();
        runtime.local_history.reserve(config.frame_count);
    }
    auto fail(Error error) noexcept -> Result<PeerStep> {
        failure = error; observation.phase = PeerPhase::failed;
        return Result<PeerStep>::failure(error);
    }
};

PeerDriver::PeerDriver(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
PeerDriver::~PeerDriver() = default;
auto PeerDriver::create(const PeerConfig& config, RollbackSession& session)
    -> Result<std::unique_ptr<PeerDriver>> {
    const auto report = session.report();
    const auto limit = config.engine_abi_version != 0U ? 240U : 256U;
    if (config.frame_count == 0U || config.frame_count > limit ||
        report.state.frame.value != 0U || report.local_peer != config.id) {
        return Result<std::unique_ptr<PeerDriver>>::failure(
            Error{ErrorCode::invalid_argument, config.frame_count, 0U, "udp_driver_config"});
    }
    auto socket = UdpSocket::bind_loopback(config.listen_port);
    if (!socket.ok()) return Result<std::unique_ptr<PeerDriver>>::failure(socket.error());
    auto hello = encode_hello(config);
    if (!hello.ok()) return Result<std::unique_ptr<PeerDriver>>::failure(hello.error());
    auto impl = std::make_unique<Impl>(config, std::move(socket.value()), session, std::move(hello.value()));
    return Result<std::unique_ptr<PeerDriver>>::success(
        std::unique_ptr<PeerDriver>{new PeerDriver(std::move(impl))});
}
auto PeerDriver::observation() const -> PeerStep { return impl_->observation; }
auto PeerDriver::correction() const -> const PeerCorrection& { return impl_->runtime.correction; }
auto PeerDriver::report_json() const -> const std::string& { return impl_->artifacts.report; }
auto PeerDriver::replay_bytes() const -> const std::vector<std::byte>& { return impl_->artifacts.replay; }
auto PeerDriver::diagnostic_json() const -> Result<std::string> {
    return impl_->runtime.diagnostic.has_value() ? canonical_json(*impl_->runtime.diagnostic)
        : Result<std::string>::success("null");
}
auto PeerDriver::failure_json() const -> std::string {
    const auto& i = *impl_;
    const auto error = i.failure.value_or(Error{});
    const auto diagnostic = diagnostic_json();
    std::ostringstream out;
    out << "{" << engine_identity_fields(i.config) << ",\"schema_version\":1,\"profile\":\""
        << (i.config.engine_abi_version ? "engine-udp-v1" : "legacy-udp-v1")
        << "\",\"source_git_sha\":\"" << kGitSha
        << "\",\"expected_abi_version\":" << i.config.engine_abi_version
        << ",\"protocol_version\":" << kProtocolVersion
        << ",\"simulation_version\":" << kSimulationVersion
        << ",\"scenario_seed\":" << i.config.scenario_seed
        << ",\"transport_seed\":" << i.config.transport_seed
        << ",\"target_frame\":" << i.config.frame_count
        << ",\"phase\":" << static_cast<unsigned>(i.observation.phase)
        << ",\"handshake_complete\":" << (i.observation.handshake_complete ? "true" : "false")
        << ",\"error_code\":" << static_cast<unsigned>(error.code)
        << ",\"detail\":" << error.detail << ",\"offset\":" << error.offset
        << ",\"context\":\"" << error.context << "\",\"diagnostic\":"
        << (diagnostic.ok() ? diagnostic.value() : "null") << "}\n";
    return out.str();
}
auto PeerDriver::step(const std::uint64_t elapsed) -> Result<PeerStep> try {
    auto& i = *impl_;
    if (i.failure) return Result<PeerStep>::failure(*i.failure);
    if (i.observation.phase == PeerPhase::finished) return Result<PeerStep>::success(i.observation);
    if (elapsed < i.last_elapsed) return Result<PeerStep>::failure(
        Error{ErrorCode::invalid_argument, elapsed, 0U, "udp_clock_reversed"});
    i.last_elapsed = elapsed;
    i.runtime.correction = {};
    if (i.observation.phase == PeerPhase::handshake) {
        if (elapsed >= i.config.handshake_timeout_milliseconds)
            return i.fail(Error{ErrorCode::timeout, elapsed, 0U, "peer_handshake"});
        const auto sent = i.socket.send_loopback(i.config.relay_port, i.hello);
        if (!sent.ok()) return i.fail(sent.error());
        const auto received = i.socket.receive_for(std::chrono::milliseconds{0});
        if (!received.ok()) return i.fail(received.error());
        if (received.value()) {
            if (i.config.engine_abi_version && received.value()->source_port != i.config.relay_port)
                return i.fail(Error{ErrorCode::invalid_argument, received.value()->source_port, 0U, "udp_relay_source"});
            const auto packet = decode_packet(received.value()->bytes);
            if (!packet.ok()) return i.fail(packet.error());
            const auto valid = valid_hello(i.config, packet.value());
            if (!valid.ok()) return i.fail(valid.error());
            const auto acknowledged = i.socket.send_loopback(i.config.relay_port, i.hello);
            if (!acknowledged.ok()) return i.fail(acknowledged.error());
            i.observation.phase = PeerPhase::running;
            i.observation.handshake_complete = true;
            i.run_started = elapsed;
        }
    } else {
        if (elapsed - i.run_started >= i.config.run_timeout_milliseconds)
            return i.fail(Error{ErrorCode::timeout, i.runtime.session.report().state.frame.value, 0U, "udp_peer_run"});
        if (i.observation.phase == PeerPhase::running) {
            const auto drained = drain_available(i.config, i.socket, i.runtime);
            if (!drained.ok()) return i.fail(drained.error());
            const auto local = scripted_input(i.config.scenario_seed,
                i.runtime.session.report().state.frame, i.config.id);
            i.runtime.local_history.push_back(local);
            const auto advanced = i.runtime.session.advance(local);
            if (!advanced.ok()) return i.fail(advanced.error());
            const auto sent = send_inputs(i.config, i.socket, i.runtime);
            if (!sent.ok()) return i.fail(sent.error());
            if (i.runtime.session.report().state.frame == FrameNumber{i.config.frame_count})
                i.observation.phase = PeerPhase::confirming;
        } else {
            const auto sent = send_inputs(i.config, i.socket, i.runtime);
            if (!sent.ok()) return i.fail(sent.error());
            const auto drained = drain_available(i.config, i.socket, i.runtime);
            if (!drained.ok()) return i.fail(drained.error());
            const auto report = i.runtime.session.report();
            if (report.metrics.confirmed_frame == FrameNumber{i.config.frame_count} && i.runtime.remote_final_hash) {
                if (++i.final_drain_progress >= 16U) {
                    auto artifacts = build_peer_artifacts(i.config, i.runtime);
                    if (!artifacts.ok()) return i.fail(artifacts.error());
                    i.artifacts = std::move(artifacts.value());
                    i.observation.phase = PeerPhase::finished;
                }
            } else i.final_drain_progress = 0U;
        }
    }
    ++i.observation.logical_tick;
    return Result<PeerStep>::success(i.observation);
} catch (...) {
    // This failure transition must not allocate: even bad_alloc leaves both
    // native observations and the C ABI's sticky error in the same state.
    return impl_->fail(Error{ErrorCode::internal_failure, 0U, 0U, "udp_internal_exception"});
}

auto run_peer(const PeerConfig& config) -> Result<int> {
    SessionConfig session_config{config.id};
    session_config.variant = config.simulation_variant;
    RollbackSession session{session_config};
    auto created = PeerDriver::create(config, session);
    if (!created.ok()) return Result<int>::failure(created.error());
    auto& driver = *created.value();
    const auto start = std::chrono::steady_clock::now();
    for (;;) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        const auto step = driver.step(static_cast<std::uint64_t>(elapsed));
        if (!step.ok()) {
            if (step.error().code == ErrorCode::desync) {
                const auto diagnostic = driver.diagnostic_json();
                if (!diagnostic.ok()) return Result<int>::failure(diagnostic.error());
                std::ofstream file{config.diagnostic_path, std::ios::binary | std::ios::trunc};
                file << diagnostic.value();
                if (!file) return Result<int>::failure(Error{ErrorCode::io_error, 0U, 0U, "udp_desync_file"});
            }
            return Result<int>::failure(step.error());
        }
        if (step.value().phase == PeerPhase::finished) {
            std::ofstream report{config.report_path, std::ios::binary | std::ios::trunc};
            report << driver.report_json();
            std::ofstream replay{config.replay_path, std::ios::binary | std::ios::trunc};
            const auto& bytes = driver.replay_bytes();
            replay.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            if (!report || !replay) return Result<int>::failure(Error{ErrorCode::io_error, 0U, 0U, "udp_peer_artifacts"});
            return Result<int>::success(0);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{
            step.value().phase == PeerPhase::handshake ? 10 : 2});
    }
}

} // namespace rollback_lab
