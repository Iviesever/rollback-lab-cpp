#include "test_framework.hpp"

#include <rollback_lab/replay/replay.hpp>
#include <rollback_lab/protocol/crc32.hpp>
#include <rollback_lab/report/canonical_json.hpp>
#include <rollback_lab/report/desync.hpp>
#include <rollback_lab/report/scenario_runner.hpp>
#include <rollback_lab/report/trace.hpp>
#include <rollback_lab/simulation/scripted_input.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace {

using namespace rollback_lab;

auto make_replay(const std::uint64_t seed, const std::uint32_t frames) -> Replay {
    Replay replay{};
    replay.scenario_seed = seed;
    replay.transport_seed = 99U;
    replay.final_frame = FrameNumber{frames};
    auto world = make_initial_world();
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        const auto number = FrameNumber{frame};
        const InputPair inputs{scripted_input(seed, number, PlayerId::a),
                               scripted_input(seed, number, PlayerId::b)};
        replay.confirmed_inputs.push_back(inputs);
        const auto next = simulate_frame(world, number, inputs);
        RL_REQUIRE(next.ok());
        world = next.value();
        if (world.frame.value % 30U == 0U) {
            replay.checkpoints.push_back(
                ReplayCheckpoint{world.frame, hash_canonical(world)});
        }
    }
    replay.expected_final_hash = hash_canonical(world);
    return replay;
}

void rewrite_replay_crc(std::vector<std::byte>& bytes) {
    const auto checksum = crc32(std::span<const std::byte>{bytes}.first(bytes.size() - 4U));
    for (std::size_t index = 0; index < 4U; ++index) {
        bytes[bytes.size() - 4U + index] = static_cast<std::byte>(
            (checksum >> static_cast<unsigned>(index * 8U)) & 0xFFU);
    }
}

auto find_after(const std::string& text,
                const std::string& needle,
                const std::size_t after) -> std::size_t {
    const auto position = text.find(needle);
    RL_REQUIRE(position != std::string::npos);
    RL_CHECK(position > after);
    return position;
}

}  // namespace

RL_TEST(replay_round_trip_reconstructs_checkpoints_and_final_hash) {
    const auto replay = make_replay(123U, 180U);
    const auto encoded = encode_replay(replay);
    RL_REQUIRE(encoded.ok());
    const auto decoded = decode_replay(encoded.value());
    RL_REQUIRE(decoded.ok());
    RL_CHECK(decoded.value() == replay);
    const auto verified = verify_replay(decoded.value());
    RL_REQUIRE(verified.ok());
    RL_CHECK(verified.value().success);
    RL_CHECK(verified.value().final_frame == FrameNumber{180U});
    RL_CHECK(verified.value().actual_final_hash == replay.expected_final_hash);
}

RL_TEST(replay_rejects_corruption_unsupported_version_and_hash_mismatch) {
    const auto replay = make_replay(4U, 40U);
    const auto encoded = encode_replay(replay);
    RL_REQUIRE(encoded.ok());

    auto corrupt = encoded.value();
    corrupt[20] ^= std::byte{0x40U};
    RL_CHECK(decode_replay(corrupt).error().code ==
             ErrorCode::integrity_mismatch);

    auto unsupported = encoded.value();
    unsupported[4] = std::byte{99U};
    rewrite_replay_crc(unsupported);
    RL_CHECK(decode_replay(unsupported).error().code ==
             ErrorCode::unsupported_version);

    auto mismatch = replay;
    mismatch.expected_final_hash ^= 1U;
    const auto verified = verify_replay(mismatch);
    RL_CHECK(!verified.ok());
    RL_CHECK(verified.error().code == ErrorCode::replay_mismatch);
}

RL_TEST(canonical_report_has_fixed_order_and_timing_is_not_identity) {
    RunReport report{};
    report.git_sha = "abc123";
    report.build_type = "Debug";
    report.compiler = "MSVC 19.51";
    report.os = "Windows";
    report.scenario_seed = 7U;
    report.transport_seed = 9U;
    report.frame_count = 120U;
    report.confirmed_frame = FrameNumber{120U};
    report.final_hash_a = 0x11U;
    report.final_hash_b = 0x11U;
    report.replay_verified = true;
    report.desync_result = "none";
    report.success = true;
    report.timing.simulation_microseconds = 100U;

    const auto first_identity = report_identity(report);
    const auto first = canonical_json(report);
    RL_REQUIRE(first.ok());
    std::size_t position = find_after(first.value(), "\"git_sha\"", 0U);
    position = find_after(first.value(), "\"build_type\"", position);
    position = find_after(first.value(), "\"simulation_version\"", position);
    position = find_after(first.value(), "\"scenario_seed\"", position);
    position = find_after(first.value(), "\"rollback_count\"", position);
    position = find_after(first.value(), "\"final_hash_a\"", position);
    static_cast<void>(find_after(first.value(), "\"success\"", position));

    report.timing.simulation_microseconds = 999U;
    RL_CHECK(report_identity(report) == first_identity);
    const auto second = canonical_json(report);
    RL_REQUIRE(second.ok());
    RL_CHECK(first.value() != second.value());
}

RL_TEST(desync_requires_confirmed_hashes_and_reports_first_divergence) {
    auto canonical = make_initial_world();
    canonical.players[0].x = 100 * kSubunitsPerWorldUnit;
    canonical.players[0].y = 100 * kSubunitsPerWorldUnit;
    canonical.players[1].x = 140 * kSubunitsPerWorldUnit;
    canonical.players[1].y = 100 * kSubunitsPerWorldUnit;
    auto divergent = canonical;
    const InputPair inputs{
        InputFrame{FrameNumber{0U}, PlayerId::a, 0U,
                   button_mask(Button::attack)},
        InputFrame{FrameNumber{0U}, PlayerId::b, 0U, 0U},
    };
    const auto expected = simulate_frame(canonical, FrameNumber{0U}, inputs);
    const auto biased = simulate_frame(divergent, FrameNumber{0U}, inputs,
                                       SimulationVariant::damage_bias);
    RL_REQUIRE(expected.ok());
    RL_REQUIRE(biased.ok());

    DesyncTracker tracker{88U};
    const HashObservation local{FrameNumber{1U},
                                hash_canonical(expected.value()), true};
    const HashObservation remote{FrameNumber{1U},
                                 hash_canonical(biased.value()), false};
    RL_CHECK(!tracker.observe(local, remote, {inputs}, expected.value()).has_value());

    auto confirmed_remote = remote;
    confirmed_remote.confirmed = true;
    const auto diagnostic =
        tracker.observe(local, confirmed_remote, {inputs}, expected.value());
    RL_REQUIRE(diagnostic.has_value());
    RL_CHECK(diagnostic->earliest_divergent_frame == FrameNumber{1U});
    RL_CHECK(diagnostic->local_hash == local.hash);
    RL_CHECK(diagnostic->remote_hash == confirmed_remote.hash);
    RL_CHECK(diagnostic->scenario_seed == 88U);
    const auto json = canonical_json(diagnostic.value());
    RL_REQUIRE(json.ok());
    RL_CHECK(json.value().find("\"earliest_divergent_frame\":1") !=
             std::string::npos);
}

RL_TEST(trace_is_bounded_and_serializes_real_state) {
    Trace trace{};
    trace.scenario_seed = 55U;
    const auto state = make_initial_world();
    trace.frames.push_back(TraceFrame{FrameNumber{0U}, state,
                                      hash_canonical(state), true,
                                      FrameNumber{0U}});
    trace.rollbacks.push_back(
        RollbackTraceEvent{FrameNumber{8U}, FrameNumber{3U}, 5U});
    const auto json = canonical_json(trace);
    RL_REQUIRE(json.ok());
    RL_CHECK(json.value().find("\"x\":262144") != std::string::npos);
    RL_CHECK(json.value().find("\"rollback_from\":3") != std::string::npos);

    trace.frames.resize(kMaxTraceFrames + 1U);
    const auto oversized = canonical_json(trace);
    RL_CHECK(!oversized.ok());
    RL_CHECK(oversized.error().code == ErrorCode::capacity_exceeded);
}

RL_TEST(seeded_scenario_converges_via_packets_and_replay_is_identical) {
    ScenarioRunConfig config{};
    config.scenario_seed = 0xCAFEU;
    config.transport_seed = 0xBEEFU;
    config.frame_count = 240U;
    config.transport.seed = config.transport_seed;
    config.transport.base_latency_ticks = 5U;
    config.transport.jitter_ticks = 3U;
    config.transport.loss_percent = 5U;
    config.transport.reorder_percent = 10U;
    config.transport.duplicate_percent = 5U;
    config.transport.max_queue_packets = 4'096U;
    config.transport.max_queue_bytes = 2U << 20U;

    const auto first = run_seeded_scenario(config);
    const auto second = run_seeded_scenario(config);
    RL_REQUIRE(first.ok());
    RL_REQUIRE(second.ok());
    RL_CHECK(first.value().report.success);
    RL_CHECK(first.value().report.confirmed_frame == FrameNumber{240U});
    RL_CHECK(first.value().report.final_hash_a ==
             first.value().report.final_hash_b);
    RL_CHECK(first.value().report.replay_verified);
    RL_CHECK(first.value().report.rollback_count > 0U);
    RL_CHECK(first.value().replay == second.value().replay);
    RL_CHECK(canonical_json(first.value().report).value() ==
             canonical_json(second.value().report).value());
    RL_CHECK(canonical_json(first.value().trace).value() ==
             canonical_json(second.value().trace).value());
}
