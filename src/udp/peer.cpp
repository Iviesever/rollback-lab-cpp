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
#include <thread>
#include <utility>
#include <vector>

namespace rollback_lab {
namespace {

struct PeerRuntime final {
    RollbackSession session;
    SequenceWindow sequences;
    DesyncTracker desync;
    std::vector<InputFrame> local_history;
    std::optional<StateHash> remote_final_hash;
    TransportMetrics metrics{};
    std::uint32_t next_sequence{2U};
};

auto remote_id(const PlayerId local) -> PlayerId {
    return local == PlayerId::a ? PlayerId::b : PlayerId::a;
}

auto scenario_config_digest(const PeerConfig& config) -> StateHash {
    ByteWriter bytes;
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
    if (packet.hello->target_frame != config.frame_count ||
        packet.hello->scenario_config_digest !=
            scenario_config_digest(expected)) {
        return Result<void>::failure(
            Error{ErrorCode::invalid_argument,
                  packet.hello->scenario_config_digest, 0U,
                  "udp_scenario_config"});
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

auto wait_for_handshake(const PeerConfig& config, UdpSocket& socket)
    -> Result<void> {
    const auto hello = encode_hello(config);
    if (!hello.ok()) {
        return Result<void>::failure(hello.error());
    }
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds{config.handshake_timeout_milliseconds};
    while (std::chrono::steady_clock::now() < deadline) {
        const auto sent = socket.send_loopback(config.relay_port, hello.value());
        if (!sent.ok()) {
            return sent;
        }
        const auto received = socket.receive_for(std::chrono::milliseconds{10});
        if (!received.ok()) {
            return Result<void>::failure(received.error());
        }
        if (!received.value().has_value()) {
            continue;
        }
        const auto decoded = decode_packet(received.value()->bytes);
        if (!decoded.ok()) {
            return Result<void>::failure(decoded.error());
        }
        const auto validated = valid_hello(config, decoded.value());
        if (validated.ok()) {
            const auto acknowledged =
                socket.send_loopback(config.relay_port, hello.value());
            if (!acknowledged.ok()) {
                return acknowledged;
            }
            return Result<void>::success();
        }
        return validated;
    }
    return Result<void>::failure(
        Error{ErrorCode::timeout, config.handshake_timeout_milliseconds, 0U,
              "peer_handshake"});
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

auto write_desync_diagnostic(const PeerConfig& config,
                             const DesyncDiagnostic& diagnostic)
    -> Result<void> {
    const auto json = canonical_json(diagnostic);
    if (!json.ok()) {
        return Result<void>::failure(json.error());
    }
    std::ofstream file{config.diagnostic_path,
                       std::ios::binary | std::ios::trunc};
    file << json.value();
    if (!file) {
        return Result<void>::failure(
            Error{ErrorCode::io_error, 0U, 0U, "udp_desync_file"});
    }
    return Result<void>::success();
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
        const auto ingested = runtime.session.ingest_remote(input);
        if (!ingested.ok()) {
            return Result<bool>::failure(ingested.error());
        }
    }
    const auto corrected = runtime.session.flush_corrections();
    if (!corrected.ok()) {
        return Result<bool>::failure(corrected.error());
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
                const auto written =
                    write_desync_diagnostic(config, diagnostic.value());
                if (!written.ok()) {
                    return Result<bool>::failure(written.error());
                }
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

auto write_peer_artifacts(const PeerConfig& config,
                          const PeerRuntime& runtime) -> Result<void> {
    const auto replay = build_replay(config, runtime.session);
    if (!replay.ok()) {
        return Result<void>::failure(replay.error());
    }
    const auto verified = verify_replay(replay.value());
    const auto encoded = encode_replay(replay.value());
    if (!verified.ok() || !encoded.ok()) {
        return Result<void>::failure(
            !verified.ok() ? verified.error() : encoded.error());
    }
    const auto session = runtime.session.report();
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
        return Result<void>::failure(json.error());
    }
    std::ofstream report_file{config.report_path,
                              std::ios::binary | std::ios::trunc};
    report_file << json.value();
    std::ofstream replay_file{config.replay_path,
                              std::ios::binary | std::ios::trunc};
    replay_file.write(reinterpret_cast<const char*>(encoded.value().data()),
                      static_cast<std::streamsize>(encoded.value().size()));
    if (!report_file || !replay_file || !report.success) {
        return Result<void>::failure(
            Error{ErrorCode::io_error, 0U, 0U, "udp_peer_artifacts"});
    }
    return Result<void>::success();
}

}  // namespace

auto run_peer(const PeerConfig& config) -> Result<int> {
    auto socket_result = UdpSocket::bind_loopback(config.listen_port);
    if (!socket_result.ok()) {
        return Result<int>::failure(socket_result.error());
    }
    auto socket = std::move(socket_result.value());
    const auto handshake = wait_for_handshake(config, socket);
    if (!handshake.ok()) {
        return Result<int>::failure(handshake.error());
    }

    SessionConfig session_config{config.id};
    session_config.variant = config.simulation_variant;
    PeerRuntime runtime{RollbackSession{session_config},
                        SequenceWindow{}, DesyncTracker{config.scenario_seed},
                        {}, std::nullopt,
                        TransportMetrics{}, 2U};
    runtime.local_history.reserve(config.frame_count);
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds{config.run_timeout_milliseconds};
    for (std::uint32_t frame = 0U; frame < config.frame_count; ++frame) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return Result<int>::failure(
                Error{ErrorCode::timeout, frame, 0U, "udp_peer_run"});
        }
        const auto drained = drain_available(config, socket, runtime);
        if (!drained.ok()) {
            return Result<int>::failure(drained.error());
        }
        const auto local = scripted_input(config.scenario_seed,
                                          FrameNumber{frame}, config.id);
        runtime.local_history.push_back(local);
        const auto advanced = runtime.session.advance(local);
        if (!advanced.ok()) {
            return Result<int>::failure(advanced.error());
        }
        const auto sent = send_inputs(config, socket, runtime);
        if (!sent.ok()) {
            return Result<int>::failure(sent.error());
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{2});
    }

    constexpr std::uint32_t final_drain_iterations = 16U;
    std::uint32_t final_drain_progress = 0U;
    while (std::chrono::steady_clock::now() < deadline) {
        const auto sent = send_inputs(config, socket, runtime);
        if (!sent.ok()) {
            return Result<int>::failure(sent.error());
        }
        const auto received = receive_one(config, socket, runtime,
                                          std::chrono::milliseconds{5});
        if (!received.ok()) {
            return Result<int>::failure(received.error());
        }
        const auto report = runtime.session.report();
        if (report.metrics.confirmed_frame == FrameNumber{config.frame_count} &&
            runtime.remote_final_hash.has_value()) {
            ++final_drain_progress;
            if (final_drain_progress >= final_drain_iterations) {
                const auto artifacts = write_peer_artifacts(config, runtime);
                if (!artifacts.ok()) {
                    return Result<int>::failure(artifacts.error());
                }
                return Result<int>::success(0);
            }
        } else {
            final_drain_progress = 0U;
        }
    }
    return Result<int>::failure(
        Error{ErrorCode::timeout, config.frame_count, 0U, "udp_peer_confirm"});
}

}  // namespace rollback_lab
