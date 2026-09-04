#include "test_framework.hpp"
#include <rollback_lab/c_api/rollback_lab_c.h>
#include <rollback_lab/report/scenario_runner.hpp>
#include <rollback_lab/report/canonical_json.hpp>
#include <array>
#include <string>
#include <vector>

namespace {
using namespace rollback_lab;
template <class T> auto value() -> T {
    T result{};
    result.api_version = RL_API_VERSION;
    result.struct_size = sizeof(T);
    return result;
}
struct Handles final {
    rl_session *a{};
    rl_session *b{};
    rl_live *live{};
    ~Handles() {
        if (live)
            static_cast<void>(rl_live_destroy(live));
        if (a)
            static_cast<void>(rl_session_destroy(a));
        if (b)
            static_cast<void>(rl_session_destroy(b));
    }
    void create(const bool desync = false) {
        auto config = value<rl_session_config>();
        config.max_rollback_frames = 120U;
        RL_REQUIRE(rl_session_create(&config, &a) == RL_OK);
        config.local_peer = RL_PEER_B;
        config.simulation_variant = desync ? RL_VARIANT_DAMAGE_BIAS : RL_VARIANT_CANONICAL;
        RL_REQUIRE(rl_session_create(&config, &b) == RL_OK);
    }
};
auto config_for(const unsigned seed) -> rl_live_config {
    auto c = value<rl_live_config>();
    constexpr std::array<unsigned, 7> frames{1U, 32U, 120U, 121U, 255U, 256U, 257U};
    c.scenario_seed = seed + 1U;
    c.transport_seed = seed * 31U + 7U;
    c.frame_count = frames[seed % frames.size()];
    c.base_latency_ticks = seed % 8U;
    c.jitter_ticks = seed % 4U;
    constexpr std::array<unsigned, 4> loss{0U, 1U, 5U, 20U};
    c.loss_percent = loss[seed % loss.size()];
    c.reorder_percent = 10U;
    c.duplicate_percent = 5U;
    c.max_queue_packets = 4096U;
    c.max_queue_bytes = 4U << 20U;
    c.bandwidth_bytes_per_tick = 1U << 20U;
    c.max_packet_age_ticks = 600U;
    c.tail_redundancy_ticks = 64U;
    return c;
}
auto native_config(const rl_live_config &c) -> ScenarioRunConfig {
    ScenarioRunConfig n{};
    n.scenario_seed = c.scenario_seed;
    n.transport_seed = c.transport_seed;
    n.frame_count = c.frame_count;
    n.transport.base_latency_ticks = c.base_latency_ticks;
    n.transport.jitter_ticks = c.jitter_ticks;
    n.transport.loss_percent = c.loss_percent;
    n.transport.reorder_percent = c.reorder_percent;
    n.transport.duplicate_percent = c.duplicate_percent;
    n.transport.burst_loss_percent = c.burst_loss_percent;
    n.transport.max_queue_packets = c.max_queue_packets;
    n.transport.max_queue_bytes = c.max_queue_bytes;
    n.transport.bandwidth_bytes_per_tick = c.bandwidth_bytes_per_tick;
    n.transport.max_packet_age_ticks = c.max_packet_age_ticks;
    n.tail_redundancy_ticks = c.tail_redundancy_ticks;
    return n;
}
auto report_json(rl_live *live) -> std::string {
    uint32_t size{};
    RL_REQUIRE(rl_live_copy_report(live, nullptr, 0U, &size) == RL_BUFFER_TOO_SMALL);
    RL_REQUIRE(size > 1U);
    std::vector<char> data(size);
    RL_REQUIRE(rl_live_copy_report(live, data.data(), size, &size) == RL_OK);
    RL_CHECK(data.back() == '\0');
    return data.data();
}
} // namespace

RL_TEST(c_live_128_scenarios_match_direct_cpp_report_replay_hash) {
    for (unsigned seed = 0; seed < 128U; ++seed) {
        auto config = config_for(seed);
        Handles h;
        h.create();
        RL_CHECK(h.a != h.b);
        RL_REQUIRE(rl_live_create(&config, h.a, h.b, &h.live) == RL_OK);
        auto step = value<rl_live_step_result>();
        for (unsigned tick = 0; tick < config.frame_count + 96U; ++tick) {
            RL_REQUIRE(rl_live_step(h.live, 0U, 0U, &step) == RL_OK);
            RL_CHECK(step.logical_tick == tick + 1U);
        }
        RL_CHECK(step.finished == 1U);
        RL_CHECK(step.desync_detected == 0U);
        auto direct = run_seeded_scenario(native_config(config));
        RL_REQUIRE(direct.ok());
        RL_CHECK(report_json(h.live) == canonical_json(direct.value().report).value());
        auto sa = value<rl_world_snapshot>();
        auto sb = value<rl_world_snapshot>();
        RL_REQUIRE(rl_session_get_snapshot(h.a, &sa) == RL_OK);
        RL_REQUIRE(rl_session_get_snapshot(h.b, &sb) == RL_OK);
        RL_CHECK(sa.state_hash == direct.value().report.final_hash_a);
        RL_CHECK(sb.state_hash == sa.state_hash);
        uint32_t count{};
        RL_REQUIRE(rl_live_copy_replay(h.live, nullptr, 0U, &count) == RL_BUFFER_TOO_SMALL);
        std::vector<uint8_t> bytes(count);
        RL_REQUIRE(rl_live_copy_replay(h.live, bytes.data(), count, &count) == RL_OK);
        const auto expected = encode_replay(direct.value().replay);
        RL_REQUIRE(expected.ok());
        RL_CHECK(bytes.size() == expected.value().size());
        for (size_t index = 0; index < bytes.size(); ++index)
            RL_CHECK(bytes[index] == std::to_integer<uint8_t>(expected.value()[index]));
    }
}

RL_TEST(c_live_borrow_guards_and_struct_validation_precede_mutation) {
    Handles h;
    h.create();
    auto config = config_for(5U);
    rl_live *invalid{};
    RL_CHECK(rl_live_create(&config, h.a, h.a, &invalid) == RL_INVALID_PEER);
    config.struct_size--;
    RL_CHECK(rl_live_create(&config, h.a, h.b, &invalid) == RL_STRUCT_SIZE);
    config.struct_size++;
    RL_REQUIRE(rl_live_create(&config, h.a, h.b, &h.live) == RL_OK);
    RL_CHECK(rl_session_destroy(h.a) == RL_BORROWED);
    auto input = value<rl_input_frame>();
    auto advance = value<rl_advance_result>();
    RL_CHECK(rl_session_advance(h.a, &input, &advance) == RL_BORROWED);
    auto step = value<rl_live_step_result>();
    step.api_version++;
    RL_CHECK(rl_live_step(h.live, 0, 0, &step) == RL_ABI_VERSION);
    step.api_version = RL_API_VERSION;
    RL_CHECK(rl_live_step(h.live, 2, 0, &step) == RL_INVALID_ARGUMENT);
    RL_REQUIRE(rl_live_step(h.live, 0, 0, &step) == RL_OK);
    RL_CHECK(step.logical_tick == 1U);
    auto correction = value<rl_live_correction>();
    RL_CHECK(rl_live_get_correction(h.live, 2, &correction) == RL_INVALID_PEER);
    uint32_t count{};
    RL_CHECK(rl_live_copy_report(h.live, nullptr, 0, &count) == RL_INVALID_FRAME);
    RL_REQUIRE(rl_live_copy_trace(h.live, nullptr, 0, &count) == RL_BUFFER_TOO_SMALL);
    std::array<char, 2> tiny{'x', 'y'};
    RL_CHECK(rl_live_copy_trace(h.live, tiny.data(), 2, &count) == RL_BUFFER_TOO_SMALL);
    RL_CHECK(tiny[0] == 'x' && tiny[1] == 'y');
    RL_REQUIRE(rl_live_destroy(h.live) == RL_OK);
    h.live = nullptr;
    RL_CHECK(rl_session_destroy(h.a) == RL_OK);
    h.a = nullptr;
}

RL_TEST(c_live_correction_is_real_and_desync_uses_confirmed_boundary) {
    Handles h;
    h.create(true);
    auto config = config_for(4U);
    config.scenario_seed = 1U;
    RL_REQUIRE(rl_live_create(&config, h.a, h.b, &h.live) == RL_OK);
    bool correction_seen = false;
    bool desync_seen = false;
    auto step = value<rl_live_step_result>();
    for (unsigned tick = 0; tick < config.frame_count + 96U; ++tick) {
        RL_REQUIRE(rl_live_step(h.live, 0, 0, &step) == RL_OK);
        for (uint32_t peer = 0; peer < 2U; ++peer) {
            auto c = value<rl_live_correction>();
            RL_REQUIRE(rl_live_get_correction(h.live, peer, &c) == RL_OK);
            if (c.performed) {
                correction_seen = true;
                RL_CHECK(c.before.frame == c.after.frame);
                RL_CHECK(c.rollback_from < c.before.frame);
                RL_CHECK(c.resimulated_frames <= 120U);
            }
        }
        if (step.desync_detected) {
            desync_seen = true;
            auto ca = value<rl_frame_result>();
            auto cb = value<rl_frame_result>();
            RL_REQUIRE(rl_session_get_confirmed_frame(h.a, &ca) == RL_OK);
            RL_REQUIRE(rl_session_get_confirmed_frame(h.b, &cb) == RL_OK);
            RL_CHECK(step.earliest_divergent_frame <= ca.frame);
            RL_CHECK(step.earliest_divergent_frame <= cb.frame);
        }
    }
    RL_CHECK(correction_seen);
    RL_CHECK(desync_seen);
    RL_CHECK(step.finished);
    RL_CHECK(report_json(h.live).find("\"desync_result\":\"detected\"") != std::string::npos);
}

RL_TEST(c_live_rejects_symmetric_wrong_simulation_against_canonical_replay) {
    Handles h;
    auto session = value<rl_session_config>();
    session.max_rollback_frames = 120U;
    session.simulation_variant = RL_VARIANT_DAMAGE_BIAS;
    RL_REQUIRE(rl_session_create(&session, &h.a) == RL_OK);
    session.local_peer = RL_PEER_B;
    RL_REQUIRE(rl_session_create(&session, &h.b) == RL_OK);
    auto config = config_for(4U);
    config.scenario_seed = 1U;
    RL_REQUIRE(rl_live_create(&config, h.a, h.b, &h.live) == RL_OK);
    auto step = value<rl_live_step_result>();
    rl_status status = RL_OK;
    for (unsigned tick = 0; tick < config.frame_count + 96U; ++tick) {
        status = rl_live_step(h.live, 0, 0, &step);
        if (status != RL_OK) break;
    }
    auto hash_a = value<rl_hash_result>();
    auto hash_b = value<rl_hash_result>();
    RL_REQUIRE(rl_session_get_hash(h.a, &hash_a) == RL_OK);
    RL_REQUIRE(rl_session_get_hash(h.b, &hash_b) == RL_OK);
    const auto canonical = run_seeded_scenario(native_config(config));
    RL_REQUIRE(canonical.ok());
    RL_CHECK(hash_a.state_hash == hash_b.state_hash);
    RL_CHECK(hash_a.state_hash != canonical.value().replay.expected_final_hash);
    RL_CHECK(status == RL_REPLAY_MISMATCH);
    RL_CHECK(rl_live_step(h.live, 0, 0, &step) == RL_REPLAY_MISMATCH);
    uint32_t count{};
    RL_CHECK(rl_live_copy_trace(h.live, nullptr, 0, &count) == RL_BUFFER_TOO_SMALL);
}
