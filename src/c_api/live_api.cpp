#include "internal.hpp"
#include <rollback_lab/report/live_scenario.hpp>
#include <rollback_lab/report/canonical_json.hpp>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>

struct rl_live final {
    rl_session *a;
    rl_session *b;
    std::unique_ptr<rollback_lab::LiveScenario> native;
    const std::thread::id owner{std::this_thread::get_id()};
    bool finished{};
    rl_status failure{RL_OK};
};

namespace {
using namespace rollback_lab;
using namespace rl_detail;
auto check_live(const rl_live *live) noexcept -> rl_status {
    if (!live)
        return RL_INVALID_ARGUMENT;
    return live->owner == std::this_thread::get_id() ? RL_OK : RL_WRONG_THREAD;
}
auto validate_config(const rl_live_config *c) noexcept -> rl_status {
    if (const auto status = validate(c); status != RL_OK)
        return status;
    // Explicit SDK facade caps bound storage and failed transport runs.
    if (!c->frame_count || c->frame_count > 36'000U || c->base_latency_ticks > 10'000U ||
        c->jitter_ticks > 10'000U || c->loss_percent > 100U || c->reorder_percent > 100U ||
        c->duplicate_percent > 100U || c->burst_loss_percent > 100U || !c->max_queue_packets ||
        c->max_queue_packets > 65'536U || !c->max_queue_bytes ||
        c->max_queue_bytes > (64U << 20U) || !c->bandwidth_bytes_per_tick ||
        c->bandwidth_bytes_per_tick > (64U << 20U) || !c->max_packet_age_ticks ||
        c->max_packet_age_ticks > 100'000U || c->tail_redundancy_ticks > 4'096U)
        return RL_INVALID_ARGUMENT;
    return RL_OK;
}
auto native_config(const rl_live_config &c) -> ScenarioRunConfig {
    ScenarioRunConfig n{};
    n.scenario_seed = c.scenario_seed;
    n.transport_seed = c.transport_seed;
    n.frame_count = c.frame_count;
    n.tail_redundancy_ticks = c.tail_redundancy_ticks;
    n.capture_trace = true;
    auto &t = n.transport;
    t.base_latency_ticks = c.base_latency_ticks;
    t.jitter_ticks = c.jitter_ticks;
    t.loss_percent = c.loss_percent;
    t.reorder_percent = c.reorder_percent;
    t.duplicate_percent = c.duplicate_percent;
    t.burst_loss_percent = c.burst_loss_percent;
    t.max_queue_packets = c.max_queue_packets;
    t.max_queue_bytes = c.max_queue_bytes;
    t.bandwidth_bytes_per_tick = c.bandwidth_bytes_per_tick;
    t.max_packet_age_ticks = c.max_packet_age_ticks;
    return n;
}
auto copy_bytes(std::span<const std::byte> bytes, void *buffer, uint32_t capacity,
                uint32_t *required) -> rl_status {
    if (!required || (!buffer && capacity != 0U))
        return RL_INVALID_ARGUMENT;
    if (bytes.size() > std::numeric_limits<uint32_t>::max())
        return RL_CAPACITY;
    *required = static_cast<uint32_t>(bytes.size());
    if (!buffer || capacity < *required)
        return RL_BUFFER_TOO_SMALL;
    std::memcpy(buffer, bytes.data(), bytes.size());
    return RL_OK;
}
auto copy_string(const std::string &text, char *buffer, uint32_t capacity, uint32_t *required)
    -> rl_status {
    return copy_bytes(std::as_bytes(std::span{text.c_str(), text.size() + 1U}), buffer, capacity,
                      required);
}
} // namespace

extern "C" {
rl_status rl_live_create(const rl_live_config *config, rl_session *a, rl_session *b,
                         rl_live **output) {
    return boundary([&]() -> rl_status {
        if (!output)
            return RL_INVALID_ARGUMENT;
        *output = nullptr;
        if (const auto s = validate_config(config); s != RL_OK)
            return s;
        if (const auto s = check_session(a, true); s != RL_OK)
            return s;
        if (const auto s = check_session(b, true); s != RL_OK)
            return s;
        if (a == b || a->local_peer != RL_PEER_A || b->local_peer != RL_PEER_B)
            return RL_INVALID_PEER;
        if (a->touched || b->touched)
            return RL_INVALID_FRAME;
        auto created = LiveScenario::create(native_config(*config), a->native, b->native);
        if (!created.ok())
            return to_status(created.error().code);
        auto live = std::make_unique<rl_live>(rl_live{a, b, std::move(created.value())});
        // Publish borrowing only after all allocations succeed.
        a->borrowed = true;
        b->borrowed = true;
        *output = live.release();
        return RL_OK;
    });
}
rl_status rl_live_destroy(rl_live *live) {
    return boundary([&]() -> rl_status {
        if (const auto s = check_live(live); s != RL_OK)
            return s;
        live->native.reset();
        live->a->borrowed = false;
        live->b->borrowed = false;
        delete live;
        return RL_OK;
    });
}
rl_status rl_live_step(rl_live *live, uint32_t override_local_input, uint32_t local_buttons,
                       rl_live_step_result *output) {
    return boundary([&]() -> rl_status {
        if (const auto s = check_live(live); s != RL_OK)
            return s;
        if (const auto s = validate(output); s != RL_OK)
            return s;
        if (override_local_input > 1U || local_buttons > 31U)
            return RL_INVALID_ARGUMENT;
        if (live->failure != RL_OK)
            return live->failure;
        // An unexpected exception after partial progress stays fail closed.
        live->failure = RL_INTERNAL_FAILURE;
        live->a->touched = true;
        live->b->touched = true;
        const auto step = live->native->step(
            override_local_input ? std::optional{static_cast<uint8_t>(local_buttons)}
                                 : std::nullopt);
        if (!step.ok()) {
            live->failure = to_status(step.error().code);
            return live->failure;
        }
        live->failure = RL_OK;
        live->finished = step.value();
        auto result = initialized<rl_live_step_result>();
        result.logical_tick = live->native->logical_tick();
        result.finished = live->finished ? 1U : 0U;
        const auto &desync = live->native->artifacts().trace.desync;
        result.desync_detected = desync.has_value() ? 1U : 0U;
        result.earliest_divergent_frame = desync.has_value() ? desync->frame.value : 0U;
        *output = result;
        return RL_OK;
    });
}
rl_status rl_live_get_correction(rl_live *live, uint32_t peer, rl_live_correction *output) {
    return boundary([&]() -> rl_status {
        if (const auto s = check_live(live); s != RL_OK)
            return s;
        if (const auto s = validate(output); s != RL_OK)
            return s;
        if (peer > RL_PEER_B)
            return RL_INVALID_PEER;
        const auto &c = live->native->correction(static_cast<PlayerId>(peer));
        auto result = initialized<rl_live_correction>();
        result.performed = c.result.performed ? 1U : 0U;
        result.rollback_from = c.result.rollback_from.value;
        result.resimulated_frames = c.result.resimulated_frames;
        copy_snapshot(c.before, result.before);
        copy_snapshot(c.after, result.after);
        *output = result;
        return RL_OK;
    });
}
rl_status rl_live_copy_report(rl_live *live, char *buffer, uint32_t capacity, uint32_t *required) {
    return boundary([&]() -> rl_status {
        if (const auto s = check_live(live); s != RL_OK)
            return s;
        if (!live->finished)
            return RL_INVALID_FRAME;
        const auto json = canonical_json(live->native->artifacts().report);
        if (!json.ok())
            return to_status(json.error().code);
        return copy_string(json.value(), buffer, capacity, required);
    });
}
rl_status rl_live_copy_trace(rl_live *live, char *buffer, uint32_t capacity, uint32_t *required) {
    return boundary([&]() -> rl_status {
        if (const auto s = check_live(live); s != RL_OK)
            return s;
        const auto json = canonical_json(live->native->artifacts().trace);
        if (!json.ok())
            return to_status(json.error().code);
        return copy_string(json.value(), buffer, capacity, required);
    });
}
rl_status rl_live_copy_replay(rl_live *live, uint8_t *buffer, uint32_t capacity,
                              uint32_t *required) {
    return boundary([&]() -> rl_status {
        if (const auto s = check_live(live); s != RL_OK)
            return s;
        if (!live->finished)
            return RL_INVALID_FRAME;
        const auto replay = encode_replay(live->native->artifacts().replay);
        if (!replay.ok())
            return to_status(replay.error().code);
        return copy_bytes(replay.value(), buffer, capacity, required);
    });
}
}
