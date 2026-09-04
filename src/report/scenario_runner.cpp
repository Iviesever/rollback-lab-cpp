#include <rollback_lab/report/live_scenario.hpp>

#include <rollback_lab/netcode/session.hpp>
#include <rollback_lab/protocol/codec.hpp>
#include <rollback_lab/protocol/sequence_window.hpp>
#include <rollback_lab/report/desync.hpp>
#include <rollback_lab/simulation/scripted_input.hpp>

#include <algorithm>
#include <array>
#include <limits>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace rollback_lab {
namespace {

auto compiler_name() -> std::string {
#if defined(__clang__)
    return "Clang";
#elif defined(_MSC_VER)
    return "MSVC";
#elif defined(__GNUC__)
    return "GCC";
#else
    return "unknown";
#endif
}

auto os_name() -> std::string {
#if defined(_WIN32)
    return "Windows";
#elif defined(__linux__)
    return "Linux";
#elif defined(__APPLE__)
    return "macOS";
#else
    return "unknown";
#endif
}

auto build_name() -> std::string {
#if defined(NDEBUG)
    return "Release";
#else
    return "Debug";
#endif
}

auto window_from(const std::vector<InputFrame>& history)
    -> std::vector<InputFrame> {
    const auto count = std::min(history.size(), kMaxRedundantInputs);
    return std::vector<InputFrame>{history.end() -
                                       static_cast<std::ptrdiff_t>(count),
                                   history.end()};
}

auto make_packet(const RollbackSession& sender,
                 const std::vector<InputFrame>& history,
                 const std::uint64_t scenario_id,
                 const std::uint32_t sequence) -> Packet {
    const auto session = sender.report();
    Packet packet{};
    packet.type = PacketType::input;
    packet.sender = session.local_peer;
    packet.sequence = sequence;
    packet.ack = session.metrics.confirmed_frame.value;
    packet.scenario_id = scenario_id;
    packet.confirmed_frame = session.metrics.confirmed_frame;
    packet.inputs = window_from(history);
    packet.confirmed_hashes.reserve(kMaxConfirmedHashes);
    for (std::uint32_t offset = 0U; offset < kMaxConfirmedHashes; ++offset) {
        const auto frame = FrameNumber{
            static_cast<std::uint32_t>(
                session.metrics.confirmed_frame.value - offset)};
        const auto hash = sender.hash_at(frame);
        if (!hash.ok()) {
            break;
        }
        packet.confirmed_hashes.push_back(ConfirmedHash{frame, hash.value()});
    }
    std::reverse(packet.confirmed_hashes.begin(),
                 packet.confirmed_hashes.end());
    return packet;
}

auto peer_diagnostic_inputs(const RollbackSession& session,
                            const FrameNumber boundary)
    -> std::vector<InputPair> {
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

void push_packet_event(Trace& trace, PacketTraceEvent event) {
    if (trace.packets.size() < kMaxTracePacketEvents) {
        trace.packets.push_back(std::move(event));
    } else {
        ++trace.omitted_packet_events;
    }
}

void record_send_effects(Trace& trace,
                         const bool capture_trace,
                         const LogicalTick tick,
                         const Endpoint from,
                         const Endpoint to,
                         const std::uint32_t sequence,
                         const TransportMetrics before,
                         const TransportMetrics after) {
    if (!capture_trace) {
        return;
    }
    push_packet_event(
        trace, PacketTraceEvent{tick, PacketTraceKind::sent, from, to, sequence});
    if (after.dropped_loss > before.dropped_loss) {
        push_packet_event(trace, PacketTraceEvent{
            tick, PacketTraceKind::dropped, from, to, sequence});
    }
    if (after.duplicated_packets > before.duplicated_packets) {
        push_packet_event(trace, PacketTraceEvent{
            tick, PacketTraceKind::duplicated, from, to, sequence});
    }
    if (after.reordered_packets > before.reordered_packets) {
        push_packet_event(trace, PacketTraceEvent{
            tick, PacketTraceKind::reordered, from, to, sequence});
    }
}

auto send_packet(SeededTransport& transport,
                 Trace& trace,
                 const bool capture_trace,
                 const Packet& packet,
                 const Endpoint from,
                 const Endpoint to,
                 const LogicalTick tick) -> Result<void> {
    const auto encoded = encode_packet(packet);
    if (!encoded.ok()) {
        return Result<void>::failure(encoded.error());
    }
    const auto before = transport.metrics();
    const auto sent = transport.send(from, to, encoded.value(), tick);
    if (!sent.ok()) {
        return sent;
    }
    record_send_effects(trace, capture_trace, tick, from, to, packet.sequence, before,
                        transport.metrics());
    return Result<void>::success();
}

struct DeliveryContext final {
    RollbackSession& peer_a;
    RollbackSession& peer_b;
    SequenceWindow& at_a;
    SequenceWindow& at_b;
    DesyncTracker& desync_a;
    DesyncTracker& desync_b;
    Trace& trace;
    bool capture_trace{};
    std::array<LiveCorrection, 2U>& corrections;
};

auto process_delivery(const Delivery& delivery, DeliveryContext& context)
    -> Result<void> {
    const auto decoded = decode_packet(delivery.bytes);
    if (!decoded.ok()) {
        return Result<void>::failure(decoded.error());
    }
    const auto& packet = decoded.value();
    auto& receiver = delivery.to == Endpoint::a ? context.peer_a : context.peer_b;
    auto& sequence = delivery.to == Endpoint::a ? context.at_a : context.at_b;
    const auto disposition = sequence.observe(packet.sequence);
    if (context.capture_trace) {
        push_packet_event(context.trace, PacketTraceEvent{
            delivery.delivered_at, PacketTraceKind::delivered, delivery.from,
            delivery.to, packet.sequence});
    }
    if (disposition == SequenceDisposition::duplicate ||
        disposition == SequenceDisposition::stale) {
        return Result<void>::success();
    }
    for (const auto& input : packet.inputs) {
        const auto ingested = receiver.ingest_remote(input);
        if (!ingested.ok()) {
            return ingested;
        }
    }
    const auto before_correction = receiver.report().state;
    const auto correction = receiver.flush_corrections();
    if (!correction.ok()) {
        return Result<void>::failure(correction.error());
    }
    if (correction.value().performed) {
        auto& event = context.corrections[delivery.to == Endpoint::a ? 0U : 1U];
        event = LiveCorrection{correction.value(), before_correction,
                               receiver.report().state};
    }
    if (context.capture_trace && correction.value().performed) {
        if (context.trace.rollbacks.size() < kMaxTraceRollbackEvents) {
            context.trace.rollbacks.push_back(RollbackTraceEvent{
                receiver.report().state.frame, correction.value().rollback_from,
                correction.value().resimulated_frames});
        } else {
            ++context.trace.omitted_rollback_events;
        }
    }

    for (const auto& remote_hash : packet.confirmed_hashes) {
        const auto local_hash = receiver.hash_at(remote_hash.frame);
        if (local_hash.ok()) {
            if (local_hash.value() == remote_hash.hash) {
                continue;
            }
            const auto local_state = receiver.state_at(remote_hash.frame);
            if (!local_state.ok()) {
                continue;
            }
            const HashObservation local{remote_hash.frame,
                                        local_hash.value(), true};
            const HashObservation remote{remote_hash.frame,
                                         remote_hash.hash, true};
            auto& tracker = delivery.to == Endpoint::a ? context.desync_a
                                                       : context.desync_b;
            const auto diagnostic = tracker.observe(
                local, remote, peer_diagnostic_inputs(receiver, remote_hash.frame),
                local_state.value());
            if (context.capture_trace && diagnostic.has_value()) {
                context.trace.desync = TraceDesyncMarker{
                    diagnostic->earliest_divergent_frame,
                    diagnostic->local_hash, diagnostic->remote_hash};
            }
        }
    }
    return Result<void>::success();
}

auto process_tick(SeededTransport& transport,
                  const LogicalTick tick,
                  DeliveryContext& context) -> Result<void> {
    const auto deliveries = transport.deliver(tick);
    for (const auto& delivery : deliveries) {
        const auto processed = process_delivery(delivery, context);
        if (!processed.ok()) {
            return processed;
        }
    }
    return Result<void>::success();
}

void accumulate_checkpoint(Replay& replay,
                           const WorldState& state,
                           const bool force) {
    if (force || state.frame.value % 30U == 0U) {
        replay.checkpoints.push_back(ReplayCheckpoint{state.frame, hash_canonical(state)});
    }
}

} // namespace

struct LiveScenario::Storage final {
    ScenarioRunConfig config;
    RollbackSession &peer_a;
    RollbackSession &peer_b;
    ScenarioArtifacts artifacts{};
    TransportConfig transport_config;
    SeededTransport transport;
    SequenceWindow at_a;
    SequenceWindow at_b;
    DesyncTracker desync_a;
    DesyncTracker desync_b;
    std::array<LiveCorrection, 2U> corrections{};
    DeliveryContext delivery_context;
    std::vector<InputFrame> history_a;
    std::vector<InputFrame> history_b;
    WorldState replay_state{make_initial_world()};
    std::uint32_t sequence_a{1U};
    std::uint32_t sequence_b{1U};
    std::uint32_t tick_index{};
    bool finished{};
    std::optional<Error> failure;

    static auto seeded_config(const ScenarioRunConfig &source) -> TransportConfig {
        auto result = source.transport;
        result.seed = source.transport_seed;
        return result;
    }
    Storage(const ScenarioRunConfig &source, RollbackSession &a, RollbackSession &b)
        : config(source), peer_a(a), peer_b(b), transport_config(seeded_config(source)),
          transport(transport_config), desync_a(source.scenario_seed),
          desync_b(source.scenario_seed), delivery_context{peer_a,
                                                           peer_b,
                                                           at_a,
                                                           at_b,
                                                           desync_a,
                                                           desync_b,
                                                           artifacts.trace,
                                                           source.capture_trace,
                                                           corrections} {
        artifacts.trace.scenario_seed = config.scenario_seed;
        artifacts.trace.sample_interval = trace_sample_interval(config.frame_count);
        artifacts.replay.scenario_seed = config.scenario_seed;
        artifacts.replay.transport_seed = config.transport_seed;
        artifacts.replay.final_frame = FrameNumber{config.frame_count};
        history_a.reserve(config.frame_count);
        history_b.reserve(config.frame_count);
        artifacts.replay.confirmed_inputs.reserve(config.frame_count);
        if (config.capture_trace) {
            artifacts.trace.frames.push_back(TraceFrame{FrameNumber{0U}, peer_a.report().state,
                                                        peer_a.report().final_hash, false,
                                                        FrameNumber{0U}});
        }
    }
    auto finalize() -> Result<bool> {
        const auto correction_a = peer_a.flush_corrections();
        const auto correction_b = peer_b.flush_corrections();
        if (!correction_a.ok() || !correction_b.ok()) {
            return Result<bool>::failure(!correction_a.ok() ? correction_a.error()
                                                            : correction_b.error());
        }
        const auto report_a = peer_a.report();
        const auto report_b = peer_b.report();
        const auto replay_verified = verify_replay(artifacts.replay);

        RunReport report{};
        report.build_type = build_name();
        report.compiler = compiler_name();
        report.os = os_name();
        report.scenario_seed = config.scenario_seed;
        report.transport_seed = config.transport_seed;
        report.frame_count = config.frame_count;
        report.transport_config = transport_config;
        report.transport_metrics = transport.metrics();
        report.rollback_count = report_a.metrics.rollback_count + report_b.metrics.rollback_count;
        report.resimulated_frames =
            report_a.metrics.total_resimulated_frames + report_b.metrics.total_resimulated_frames;
        report.maximum_rollback_depth = std::max(report_a.metrics.maximum_rollback_depth,
                                                 report_b.metrics.maximum_rollback_depth);
        report.predicted_input_count =
            report_a.metrics.predicted_input_count + report_b.metrics.predicted_input_count;
        report.late_input_count =
            report_a.metrics.late_input_count + report_b.metrics.late_input_count;
        report.confirmed_frame =
            frame_before(report_a.metrics.confirmed_frame, report_b.metrics.confirmed_frame)
                ? report_a.metrics.confirmed_frame
                : report_b.metrics.confirmed_frame;
        report.final_hash_a = report_a.final_hash;
        report.final_hash_b = report_b.final_hash;
        report.replay_verified = replay_verified.ok();
        const bool desync = desync_a.diagnostic().has_value() || desync_b.diagnostic().has_value();
        if (desync) {
            if (!desync_a.diagnostic().has_value()) {
                artifacts.desync_diagnostic = desync_b.diagnostic();
            } else if (!desync_b.diagnostic().has_value() ||
                       !frame_before(desync_b.diagnostic()->earliest_divergent_frame,
                                     desync_a.diagnostic()->earliest_divergent_frame)) {
                artifacts.desync_diagnostic = desync_a.diagnostic();
            } else {
                artifacts.desync_diagnostic = desync_b.diagnostic();
            }
        }
        report.desync_result = desync ? "detected" : "none";
        report.success = report.confirmed_frame == FrameNumber{config.frame_count} &&
                         report.final_hash_a == report.final_hash_b &&
                         report.final_hash_a == artifacts.replay.expected_final_hash && replay_verified.ok() &&
                         !desync;
        if (!report.success) {
            report.failure_reason =
                desync ? "confirmed state hash desync" : "peers did not converge";
        }
        artifacts.report = std::move(report);
        if (desync) {
            return Result<bool>::success(true);
        }
        if (!artifacts.report.success) {
            const auto code =
                desync ? ErrorCode::desync
                       : (artifacts.report.confirmed_frame != FrameNumber{config.frame_count}
                              ? ErrorCode::timeout
                              : ErrorCode::replay_mismatch);
            return Result<bool>::failure(
                Error{code, artifacts.report.confirmed_frame.value, 0U, "seeded_scenario"});
        }
        return Result<bool>::success(true);
    }
    auto step_once(const std::optional<std::uint8_t> local_buttons) -> Result<bool> {
        corrections[0].result.performed = false;
        corrections[1].result.performed = false;
        if (tick_index < config.frame_count) {
            const auto frame = tick_index;
            const auto number = FrameNumber{frame};
            auto input_a = scripted_input(config.scenario_seed, number, PlayerId::a);
            const auto input_b = scripted_input(config.scenario_seed, number, PlayerId::b);
            if (local_buttons.has_value()) {
                input_a.buttons = *local_buttons;
            }
            history_a.push_back(input_a);
            history_b.push_back(input_b);
            artifacts.replay.confirmed_inputs.push_back(InputPair{input_a, input_b});

            const auto direct = simulate_frame(replay_state, number, InputPair{input_a, input_b});
            if (!direct.ok()) {
                return Result<bool>::failure(direct.error());
            }
            replay_state = direct.value();
            accumulate_checkpoint(artifacts.replay, replay_state, frame + 1U == config.frame_count);

            const auto tick = LogicalTick{frame};
            const auto packet_a =
                make_packet(peer_a, history_a, config.scenario_seed, sequence_a++);
            const auto packet_b =
                make_packet(peer_b, history_b, config.scenario_seed, sequence_b++);
            const auto sent_a = send_packet(transport, artifacts.trace, config.capture_trace,
                                            packet_a, Endpoint::a, Endpoint::b, tick);
            const auto sent_b = send_packet(transport, artifacts.trace, config.capture_trace,
                                            packet_b, Endpoint::b, Endpoint::a, tick);
            if (!sent_a.ok()) {
                return Result<bool>::failure(sent_a.error());
            }
            if (!sent_b.ok()) {
                return Result<bool>::failure(sent_b.error());
            }
            const auto processed = process_tick(transport, tick, delivery_context);
            if (!processed.ok()) {
                return Result<bool>::failure(processed.error());
            }

            const auto advanced_a = peer_a.advance(input_a);
            const auto advanced_b = peer_b.advance(input_b);
            if (!advanced_a.ok()) {
                return Result<bool>::failure(advanced_a.error());
            }
            if (!advanced_b.ok()) {
                return Result<bool>::failure(advanced_b.error());
            }

            if (config.capture_trace && ((frame + 1U) % artifacts.trace.sample_interval == 0U ||
                                         frame + 1U == config.frame_count)) {
                const auto report = peer_a.report();
                if (artifacts.trace.frames.size() < kMaxTraceFrames) {
                    artifacts.trace.frames.push_back(
                        TraceFrame{report.state.frame, report.state, report.final_hash,
                                   report.metrics.confirmed_frame != report.state.frame,
                                   report.metrics.confirmed_frame});
                } else {
                    ++artifacts.trace.omitted_frame_samples;
                }
            }

            if (tick_index + 1U == config.frame_count) {
                artifacts.replay.expected_final_hash = hash_canonical(replay_state);
            }
        } else if (tick_index < config.frame_count + config.tail_redundancy_ticks) {
            const auto tail = tick_index - config.frame_count;
            const auto tick = LogicalTick{config.frame_count + tail};
            const auto sent_a =
                send_packet(transport, artifacts.trace, config.capture_trace,
                            make_packet(peer_a, history_a, config.scenario_seed, sequence_a++),
                            Endpoint::a, Endpoint::b, tick);
            const auto sent_b =
                send_packet(transport, artifacts.trace, config.capture_trace,
                            make_packet(peer_b, history_b, config.scenario_seed, sequence_b++),
                            Endpoint::b, Endpoint::a, tick);
            if (!sent_a.ok() || !sent_b.ok()) {
                return Result<bool>::failure(!sent_a.ok() ? sent_a.error() : sent_b.error());
            }
            const auto processed = process_tick(transport, tick, delivery_context);
            if (!processed.ok()) {
                return Result<bool>::failure(processed.error());
            }

        } else {
            const auto processed =
                process_tick(transport, LogicalTick{tick_index}, delivery_context);
            if (!processed.ok()) {
                return Result<bool>::failure(processed.error());
            }
        }
        ++tick_index;
        if (tick_index == config.frame_count + config.tail_redundancy_ticks + 32U) {
            return finalize();
        }
        return Result<bool>::success(false);
    }
};

LiveScenario::LiveScenario(std::unique_ptr<Storage> storage) : storage_(std::move(storage)) {}
LiveScenario::~LiveScenario() = default;

auto LiveScenario::create(const ScenarioRunConfig &config, RollbackSession &peer_a,
                          RollbackSession &peer_b) -> Result<std::unique_ptr<LiveScenario>> {
    if (config.frame_count == 0U || config.frame_count > kMaxReplayFrames ||
        config.tail_redundancy_ticks >
            std::numeric_limits<std::uint32_t>::max() - config.frame_count - 32U) {
        return Result<std::unique_ptr<LiveScenario>>::failure(
            Error{ErrorCode::invalid_argument, config.frame_count, 0U, "scenario_frame_count"});
    }
    if (&peer_a == &peer_b || peer_a.report().local_peer != PlayerId::a ||
        peer_b.report().local_peer != PlayerId::b ||
        peer_a.report().state != make_initial_world() ||
        peer_b.report().state != make_initial_world()) {
        return Result<std::unique_ptr<LiveScenario>>::failure(
            Error{ErrorCode::invalid_argument, 0U, 0U, "live_session_ownership"});
    }
    return Result<std::unique_ptr<LiveScenario>>::success(std::unique_ptr<LiveScenario>{
        new LiveScenario{std::make_unique<Storage>(config, peer_a, peer_b)}});
}

auto LiveScenario::step(const std::optional<std::uint8_t> local_buttons) -> Result<bool> {
    if (storage_->failure.has_value()) {
        return Result<bool>::failure(*storage_->failure);
    }
    if (storage_->finished) {
        return Result<bool>::success(true);
    }
    if (local_buttons.has_value() && (*local_buttons & 0xE0U) != 0U) {
        return Result<bool>::failure(
            Error{ErrorCode::invalid_argument, *local_buttons, 0U, "live_buttons"});
    }
    const auto result = storage_->step_once(local_buttons);
    if (!result.ok()) {
        storage_->failure = result.error();
    } else {
        storage_->finished = result.value();
    }
    return result;
}
auto LiveScenario::logical_tick() const noexcept -> std::uint32_t { return storage_->tick_index; }
auto LiveScenario::artifacts() const noexcept -> const ScenarioArtifacts & {
    return storage_->artifacts;
}
auto LiveScenario::correction(const PlayerId peer) const noexcept -> const LiveCorrection & {
    return storage_->corrections[peer == PlayerId::a ? 0U : 1U];
}

auto run_seeded_scenario(const ScenarioRunConfig &config) -> Result<ScenarioArtifacts> {
    RollbackSession peer_a{SessionConfig{PlayerId::a}};
    SessionConfig peer_b_config{PlayerId::b};
    peer_b_config.variant = config.peer_b_variant;
    RollbackSession peer_b{peer_b_config};
    auto created = LiveScenario::create(config, peer_a, peer_b);
    if (!created.ok()) {
        return Result<ScenarioArtifacts>::failure(created.error());
    }
    auto &run = *created.value();
    for (std::uint32_t tick = 0U; tick < config.frame_count + config.tail_redundancy_ticks + 32U;
         ++tick) {
        const auto result = run.step();
        if (!result.ok()) {
            return Result<ScenarioArtifacts>::failure(result.error());
        }
        if (result.value()) {
            return Result<ScenarioArtifacts>::success(run.artifacts());
        }
    }
    return Result<ScenarioArtifacts>::failure(
        Error{ErrorCode::timeout, run.logical_tick(), 0U, "live_runner"});
}

} // namespace rollback_lab
