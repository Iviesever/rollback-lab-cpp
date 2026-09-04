#include "test_framework.hpp"

#include <rollback_lab/report/live_scenario.hpp>
#include <rollback_lab/report/canonical_json.hpp>
#include <rollback_lab/simulation/scripted_input.hpp>

#include <fstream>
#include <filesystem>
#include <iterator>

namespace {
using namespace rollback_lab;
auto sample_config() -> ScenarioRunConfig {
    ScenarioRunConfig config{};
    config.scenario_seed = 0xC0FFEEU;
    config.transport_seed = 0x51A7E5U;
    config.frame_count = 240U;
    config.transport.base_latency_ticks = 5U;
    config.transport.jitter_ticks = 3U;
    config.transport.loss_percent = 5U;
    config.transport.reorder_percent = 10U;
    config.transport.duplicate_percent = 5U;
    config.transport.burst_loss_percent = 1U;
    config.transport.max_queue_packets = 4096U;
    config.transport.max_queue_bytes = 4U << 20U;
    return config;
}
auto sample_bytes(const char *filename) -> std::string {
    const auto source = std::filesystem::path{__FILE__}.parent_path().parent_path().parent_path();
    std::ifstream stream{source / "samples" / filename, std::ios::binary};
    RL_REQUIRE(stream.good());
    return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}
} // namespace

RL_TEST(live_driver_preserves_01_sample_identity_replay_and_trace) {
    RollbackSession a{SessionConfig{PlayerId::a}};
    RollbackSession b{SessionConfig{PlayerId::b}};
    auto created = LiveScenario::create(sample_config(), a, b);
    RL_REQUIRE(created.ok());
    auto &run = *created.value();
    bool done = false;
    bool correction_seen = false;
    bool differing_projection_seen = false;
    for (unsigned tick = 0; tick < 336U; ++tick) {
        const auto step = run.step();
        RL_REQUIRE(step.ok());
        done = step.value();
        RL_CHECK(run.logical_tick() == tick + 1U);
        RL_CHECK(a.report().state.frame.value == (tick < 240U ? tick + 1U : 240U));
        differing_projection_seen |= a.report().state != b.report().state;
        for (const auto peer : {PlayerId::a, PlayerId::b}) {
            const auto &correction = run.correction(peer);
            if (correction.result.performed) {
                correction_seen = true;
                RL_CHECK(correction.before.frame == correction.after.frame);
                RL_CHECK(correction.result.resimulated_frames > 0U);
                RL_CHECK(correction.result.resimulated_frames <= 120U);
            }
        }
        RL_CHECK(done == (tick == 335U));
    }
    RL_CHECK(correction_seen);
    RL_CHECK(differing_projection_seen);
    const auto &artifacts = run.artifacts();
    RL_CHECK(artifacts.report.success);
    RL_CHECK(report_identity(artifacts.report) == 0x8150FEDA020B66A8ULL);
    RL_CHECK(artifacts.report.final_hash_a == 0x4B35DC3FD8F6009CULL);
    RL_CHECK(artifacts.report.final_hash_a == artifacts.report.final_hash_b);
    RL_CHECK(artifacts.report.rollback_count == 189U);
    RL_CHECK(artifacts.report.resimulated_frames == 857U);
    const auto replay = encode_replay(artifacts.replay);
    RL_REQUIRE(replay.ok());
    const auto stored = sample_bytes("input.rlr");
    RL_CHECK(replay.value().size() == stored.size());
    RL_CHECK(std::string(reinterpret_cast<const char *>(replay.value().data()),
                         replay.value().size()) == stored);
    const auto trace = canonical_json(artifacts.trace);
    RL_REQUIRE(trace.ok());
    RL_CHECK(trace.value() == sample_bytes("trace.json"));
    RL_CHECK(run.step().value());
    RL_CHECK(run.logical_tick() == 336U);
}

RL_TEST(live_driver_samples_interactive_local_input_and_replays_it) {
    auto config = sample_config();
    config.frame_count = 120U;
    RollbackSession a{SessionConfig{PlayerId::a}};
    RollbackSession b{SessionConfig{PlayerId::b}};
    auto created = LiveScenario::create(config, a, b);
    RL_REQUIRE(created.ok());
    auto direct = make_initial_world();
    for (unsigned tick = 0; tick < 216U; ++tick) {
        const auto buttons = static_cast<std::uint8_t>(tick < 60U ? button_mask(Button::right)
                                                                  : button_mask(Button::attack));
        if (tick < 120U) {
            auto input_a = scripted_input(config.scenario_seed, FrameNumber{tick}, PlayerId::a);
            input_a.buttons = buttons;
            auto input_b = scripted_input(config.scenario_seed, FrameNumber{tick}, PlayerId::b);
            const auto simulated =
                simulate_frame(direct, FrameNumber{tick}, InputPair{input_a, input_b});
            RL_REQUIRE(simulated.ok());
            direct = simulated.value();
        }
        RL_REQUIRE(created.value()->step(buttons).ok());
    }
    RL_CHECK(a.report().state == direct);
    RL_CHECK(b.report().state == direct);
    RL_CHECK(verify_replay(created.value()->artifacts().replay).ok());
}

RL_TEST(live_driver_rejects_shared_wrong_or_used_sessions) {
    RollbackSession a{SessionConfig{PlayerId::a}};
    RollbackSession b{SessionConfig{PlayerId::b}};
    RL_CHECK(!LiveScenario::create(sample_config(), a, a).ok());
    RL_CHECK(!LiveScenario::create(sample_config(), b, a).ok());
    RL_REQUIRE(a.advance(scripted_input(1U, FrameNumber{0U}, PlayerId::a)).ok());
    RL_CHECK(!LiveScenario::create(sample_config(), a, b).ok());
}
