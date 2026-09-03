#include "test_framework.hpp"

#include <rollback_lab/core/checked_math.hpp>
#include <rollback_lab/core/frame.hpp>
#include <rollback_lab/core/hash.hpp>
#include <rollback_lab/simulation/scripted_input.hpp>
#include <rollback_lab/simulation/simulation.hpp>
#include <rollback_lab/simulation/state.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <vector>

namespace {

using namespace rollback_lab;

auto no_input(const FrameNumber frame) -> InputPair {
    return InputPair{
        InputFrame{frame, PlayerId::a, frame.value, 0U},
        InputFrame{frame, PlayerId::b, frame.value, 0U},
    };
}

auto run_script(const std::uint64_t seed, const std::uint32_t frames)
    -> std::vector<StateHash> {
    auto world = make_initial_world();
    std::vector<StateHash> hashes;
    hashes.reserve(frames);
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        const auto number = FrameNumber{frame};
        const InputPair inputs{
            scripted_input(seed, number, PlayerId::a),
            scripted_input(seed, number, PlayerId::b),
        };
        const auto next = simulate_frame(world, number, inputs);
        RL_REQUIRE(next.ok());
        world = next.value();
        hashes.push_back(hash_canonical(world));
    }
    return hashes;
}

auto active_projectiles(const WorldState& world) -> std::size_t {
    return static_cast<std::size_t>(std::count_if(
        world.projectiles.begin(), world.projectiles.end(),
        [](const ProjectileState& projectile) { return projectile.active; }));
}

}  // namespace

RL_TEST(frame_order_handles_wrap_without_signed_overflow) {
    RL_CHECK(frame_before(FrameNumber{10U}, FrameNumber{11U}));
    RL_CHECK(!frame_before(FrameNumber{11U}, FrameNumber{10U}));
    RL_CHECK(frame_before(FrameNumber{0xFFFFFFFFU}, FrameNumber{0U}));
    RL_CHECK(frame_distance(FrameNumber{0xFFFFFFFEU}, FrameNumber{1U}) == 3U);
    RL_CHECK(frame_ring_index(FrameNumber{257U}, 256U) == 1U);
}

RL_TEST(checked_math_fails_closed_at_int32_boundaries) {
    const auto overflow = checked_add_i32(std::numeric_limits<std::int32_t>::max(), 1);
    const auto underflow = checked_add_i32(std::numeric_limits<std::int32_t>::min(), -1);
    const auto valid = checked_add_i32(100, -40);
    RL_CHECK(!overflow.ok());
    RL_CHECK(overflow.error().code == ErrorCode::arithmetic_overflow);
    RL_CHECK(!underflow.ok());
    RL_REQUIRE(valid.ok());
    RL_CHECK(valid.value() == 60);
}

RL_TEST(canonical_state_fields_are_integer_and_fixed_capacity) {
    static_assert(std::is_integral_v<decltype(PlayerState::x)>);
    static_assert(std::is_integral_v<decltype(PlayerState::velocity_x)>);
    static_assert(std::is_integral_v<decltype(ProjectileState::x)>);
    static_assert(std::tuple_size_v<decltype(WorldState::players)> == 2U);
    static_assert(std::tuple_size_v<decltype(WorldState::projectiles)> ==
                  kMaxProjectiles);

    const auto world = make_initial_world();
    RL_CHECK(world.frame == FrameNumber{0U});
    RL_CHECK(world.players[0].id == PlayerId::a);
    RL_CHECK(world.players[1].id == PlayerId::b);
    RL_CHECK(world.players[0].hp == kStartingHp);
    RL_CHECK(active_projectiles(world) == 0U);
}

RL_TEST(scripted_input_is_random_access_and_seeded) {
    for (std::uint32_t frame = 0; frame < 300U; ++frame) {
        const auto number = FrameNumber{frame};
        RL_CHECK(scripted_input(42U, number, PlayerId::a) ==
                 scripted_input(42U, number, PlayerId::a));
    }
    RL_CHECK(scripted_input(42U, FrameNumber{81U}, PlayerId::b) !=
             scripted_input(43U, FrameNumber{81U}, PlayerId::b));
}

RL_TEST(simulation_repeats_identical_hash_history) {
    const auto first = run_script(0xC0FFEEU, 600U);
    const auto second = run_script(0xC0FFEEU, 600U);
    RL_CHECK(first == second);
    RL_CHECK(first.size() == 600U);
    RL_CHECK(first.front() != first.back());
    RL_CHECK(first.back() == 0xA263C92E66A0AA27ULL);
}

RL_TEST(simulation_rejects_wrong_frame_boundary) {
    const auto world = make_initial_world();
    const auto result = simulate_frame(world, FrameNumber{1U}, no_input(FrameNumber{1U}));
    RL_CHECK(!result.ok());
    RL_CHECK(result.error().code == ErrorCode::frame_mismatch);
}

RL_TEST(conflicting_directions_cancel_and_arena_bounds_clamp) {
    auto world = make_initial_world();
    world.players[0].x = kPlayerRadius;
    const auto frame = FrameNumber{0U};
    auto inputs = no_input(frame);
    inputs.a.buttons = button_mask(Button::left) | button_mask(Button::right) |
                       button_mask(Button::up);
    const auto next = simulate_frame(world, frame, inputs);
    RL_REQUIRE(next.ok());
    RL_CHECK(next.value().players[0].velocity_x == 0);
    RL_CHECK(next.value().players[0].x == kPlayerRadius);
    RL_CHECK(next.value().players[0].y >= kPlayerRadius);
}

RL_TEST(projectile_capacity_is_bounded_and_attack_fails_without_mutation) {
    auto world = make_initial_world();
    for (std::size_t index = 0; index < world.projectiles.size(); ++index) {
        auto& projectile = world.projectiles[index];
        projectile.active = true;
        projectile.stable_id = static_cast<std::uint32_t>(index + 1U);
        projectile.owner = PlayerId::a;
        projectile.x = kPlayerRadius;
        projectile.y = kPlayerRadius;
        projectile.velocity_x = 0;
        projectile.velocity_y = 0;
        projectile.ttl = 100U;
    }
    world.next_projectile_id = 65U;
    auto inputs = no_input(FrameNumber{0U});
    inputs.a.buttons = button_mask(Button::attack);
    const auto next = simulate_frame(world, FrameNumber{0U}, inputs);
    RL_REQUIRE(next.ok());
    RL_CHECK(active_projectiles(next.value()) == kMaxProjectiles);
    RL_CHECK(next.value().next_projectile_id == 65U);
    RL_CHECK(next.value().players[0].attack_cooldown == 0U);
}

RL_TEST(projectiles_damage_and_defeat_scores_then_respawns) {
    auto world = make_initial_world();
    world.players[0].x = 100 * kSubunitsPerWorldUnit;
    world.players[0].y = 100 * kSubunitsPerWorldUnit;
    world.players[0].facing_x = 1;
    world.players[0].facing_y = 0;
    world.players[1].x = 140 * kSubunitsPerWorldUnit;
    world.players[1].y = 100 * kSubunitsPerWorldUnit;

    for (std::uint32_t hit = 0; hit < 4U; ++hit) {
        world.players[0].attack_cooldown = 0U;
        auto inputs = no_input(world.frame);
        inputs.a.buttons = button_mask(Button::attack);
        auto next = simulate_frame(world, world.frame, inputs);
        RL_REQUIRE(next.ok());
        world = next.value();
        for (std::uint32_t step = 0; step < 4U; ++step) {
            next = simulate_frame(world, world.frame, no_input(world.frame));
            RL_REQUIRE(next.ok());
            world = next.value();
        }
    }

    RL_CHECK(world.players[0].score == 1U);
    RL_CHECK(world.players[1].hp == kStartingHp);
    RL_CHECK(world.players[1].x == kPlayerBSpawnX);
    RL_CHECK(world.players[1].y == kPlayerSpawnY);
}

RL_TEST(canonical_serialization_has_stable_little_endian_prefix) {
    const auto world = make_initial_world();
    const auto bytes = serialize_canonical(world);
    RL_CHECK(bytes.size() >= 16U);
    RL_CHECK(std::to_integer<std::uint8_t>(bytes[0]) == 1U);
    RL_CHECK(std::to_integer<std::uint8_t>(bytes[1]) == 0U);
    RL_CHECK(std::to_integer<std::uint8_t>(bytes[2]) == 0U);
    RL_CHECK(std::to_integer<std::uint8_t>(bytes[3]) == 0U);
    RL_CHECK(hash_canonical(world) != 0U);
}
