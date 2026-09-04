#include <rollback_lab/simulation/simulation.hpp>

#include <rollback_lab/core/checked_math.hpp>
#include <rollback_lab/version.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace rollback_lab {
namespace {

template <typename T>
void append_little_endian(std::vector<std::byte>& bytes, const T value) {
    using Unsigned = std::make_unsigned_t<T>;
    const auto bits = static_cast<Unsigned>(value);
    for (std::size_t index = 0; index < sizeof(T); ++index) {
        bytes.push_back(static_cast<std::byte>(
            (bits >> static_cast<unsigned>(index * 8U)) &
            static_cast<Unsigned>(0xFFU)));
    }
}

auto input_for(const InputPair& inputs, const PlayerId player)
    -> const InputFrame& {
    return player == PlayerId::a ? inputs.a : inputs.b;
}

auto axis(const bool negative, const bool positive) -> std::int32_t {
    if (negative == positive) {
        return 0;
    }
    return negative ? -1 : 1;
}

auto clamp_position(const std::int32_t value,
                    const std::int32_t lower,
                    const std::int32_t upper) -> std::int32_t {
    return std::clamp(value, lower, upper);
}

auto player_index(const PlayerId id) -> std::size_t {
    return id == PlayerId::a ? 0U : 1U;
}

void respawn(PlayerState& player) {
    player.x = player.id == PlayerId::a ? kPlayerASpawnX : kPlayerBSpawnX;
    player.y = kPlayerSpawnY;
    player.velocity_x = 0;
    player.velocity_y = 0;
    player.facing_x = player.id == PlayerId::a ? 1 : -1;
    player.facing_y = 0;
    player.hp = kStartingHp;
    player.attack_cooldown = 0U;
}

auto spawn_projectile(WorldState& world, PlayerState& player) -> Result<void> {
    const auto slot = std::find_if(
        world.projectiles.begin(), world.projectiles.end(),
        [](const ProjectileState& projectile) { return !projectile.active; });
    if (slot == world.projectiles.end()) {
        return Result<void>::success();
    }
    if (world.next_projectile_id == std::numeric_limits<std::uint32_t>::max()) {
        return Result<void>::failure(
            Error{ErrorCode::arithmetic_overflow, world.next_projectile_id, 0U,
                  "next_projectile_id"});
    }

    constexpr auto spawn_offset = 18 * kSubunitsPerWorldUnit;
    slot->active = true;
    slot->stable_id = world.next_projectile_id;
    slot->owner = player.id;
    slot->x = clamp_position(
        player.x + static_cast<std::int32_t>(player.facing_x) * spawn_offset,
        kProjectileRadius, kArenaWidth - kProjectileRadius);
    slot->y = clamp_position(
        player.y + static_cast<std::int32_t>(player.facing_y) * spawn_offset,
        kProjectileRadius, kArenaHeight - kProjectileRadius);
    slot->velocity_x =
        static_cast<std::int32_t>(player.facing_x) * kProjectileSpeed;
    slot->velocity_y =
        static_cast<std::int32_t>(player.facing_y) * kProjectileSpeed;
    slot->ttl = kProjectileLifetimeTicks;
    ++world.next_projectile_id;
    player.attack_cooldown = kAttackCooldownTicks;
    return Result<void>::success();
}

auto move_projectile(ProjectileState& projectile) -> Result<void> {
    const auto moved_x = checked_add_i32(projectile.x, projectile.velocity_x);
    const auto moved_y = checked_add_i32(projectile.y, projectile.velocity_y);
    if (!moved_x.ok() || !moved_y.ok()) {
        return Result<void>::failure(
            Error{ErrorCode::arithmetic_overflow, projectile.stable_id, 0U,
                  "projectile_move"});
    }
    projectile.x = moved_x.value();
    projectile.y = moved_y.value();
    if (projectile.ttl > 0U) {
        --projectile.ttl;
    }
    if (projectile.ttl == 0U || projectile.x < 0 ||
        projectile.x > kArenaWidth || projectile.y < 0 ||
        projectile.y > kArenaHeight) {
        projectile.active = false;
    }
    return Result<void>::success();
}

auto overlaps(const ProjectileState& projectile, const PlayerState& player)
    -> bool {
    const auto dx = static_cast<std::int64_t>(projectile.x) - player.x;
    const auto dy = static_cast<std::int64_t>(projectile.y) - player.y;
    constexpr auto hit_extent = kPlayerRadius + kProjectileRadius;
    return dx >= -hit_extent && dx <= hit_extent && dy >= -hit_extent &&
           dy <= hit_extent;
}

}  // namespace

auto make_initial_world() -> WorldState {
    WorldState state{};
    state.players[0] = PlayerState{PlayerId::a, kPlayerASpawnX,
                                   kPlayerSpawnY, 0, 0, 1, 0,
                                   kStartingHp, 0U, 0U};
    state.players[1] = PlayerState{PlayerId::b, kPlayerBSpawnX,
                                   kPlayerSpawnY, 0, 0, -1, 0,
                                   kStartingHp, 0U, 0U};
    return state;
}

auto simulate_frame(const WorldState& before,
                    const FrameNumber frame,
                    const InputPair& inputs,
                    const SimulationVariant variant) -> Result<WorldState> {
    if (before.frame != frame || inputs.a.frame != frame ||
        inputs.b.frame != frame || inputs.a.player != PlayerId::a ||
        inputs.b.player != PlayerId::b) {
        return Result<WorldState>::failure(
            Error{ErrorCode::frame_mismatch, frame.value, 0U,
                  "simulate_frame"});
    }

    WorldState world = before;
    for (auto& player : world.players) {
        if (player.attack_cooldown > 0U) {
            --player.attack_cooldown;
        }
        const auto& input = input_for(inputs, player.id);
        const auto horizontal = axis(has_button(input.buttons, Button::left),
                                     has_button(input.buttons, Button::right));
        const auto vertical = axis(has_button(input.buttons, Button::up),
                                   has_button(input.buttons, Button::down));
        player.velocity_x = horizontal * kPlayerSpeed;
        player.velocity_y = vertical * kPlayerSpeed;
        if (horizontal != 0) {
            player.facing_x = static_cast<std::int8_t>(horizontal);
            player.facing_y = 0;
        } else if (vertical != 0) {
            player.facing_x = 0;
            player.facing_y = static_cast<std::int8_t>(vertical);
        }

        const auto moved_x = checked_add_i32(player.x, player.velocity_x);
        const auto moved_y = checked_add_i32(player.y, player.velocity_y);
        if (!moved_x.ok() || !moved_y.ok()) {
            return Result<WorldState>::failure(
                Error{ErrorCode::arithmetic_overflow,
                      static_cast<std::uint64_t>(player.id), 0U,
                      "player_move"});
        }
        player.x = clamp_position(moved_x.value(), kPlayerRadius,
                                  kArenaWidth - kPlayerRadius);
        player.y = clamp_position(moved_y.value(), kPlayerRadius,
                                  kArenaHeight - kPlayerRadius);
    }

    for (auto& player : world.players) {
        const auto& input = input_for(inputs, player.id);
        if (has_button(input.buttons, Button::attack) &&
            player.attack_cooldown == 0U) {
            const auto spawned = spawn_projectile(world, player);
            if (!spawned.ok()) {
                return Result<WorldState>::failure(spawned.error());
            }
        }
    }

    std::array<std::size_t, kMaxProjectiles> projectile_order{};
    std::size_t projectile_count{};
    for (std::size_t index = 0U; index < world.projectiles.size(); ++index) {
        if (world.projectiles[index].active) {
            projectile_order[projectile_count++] = index;
        }
    }
    std::sort(projectile_order.begin(),
              projectile_order.begin() +
                  static_cast<std::ptrdiff_t>(projectile_count),
              [&world](const std::size_t left, const std::size_t right) {
                  const auto left_id = world.projectiles[left].stable_id;
                  const auto right_id = world.projectiles[right].stable_id;
                  return left_id == right_id ? left < right : left_id < right_id;
              });
    for (std::size_t order = 0U; order < projectile_count; ++order) {
        auto& projectile = world.projectiles[projectile_order[order]];
        if (!projectile.active) {
            continue;
        }
        const auto moved = move_projectile(projectile);
        if (!moved.ok()) {
            return Result<WorldState>::failure(moved.error());
        }
        if (!projectile.active) {
            continue;
        }
        const auto target_id = projectile.owner == PlayerId::a
                                   ? PlayerId::b
                                   : PlayerId::a;
        auto& target = world.players[player_index(target_id)];
        if (!overlaps(projectile, target)) {
            continue;
        }
        projectile.active = false;
        const auto damage = variant == SimulationVariant::damage_bias
                                ? static_cast<std::int16_t>(kProjectileDamage + 1)
                                : kProjectileDamage;
        target.hp = static_cast<std::int16_t>(target.hp - damage);
        if (target.hp <= 0) {
            auto& owner = world.players[player_index(projectile.owner)];
            ++owner.score;
            respawn(target);
        }
    }

    world.frame = next_frame(frame);
    return Result<WorldState>::success(std::move(world));
}

auto serialize_canonical(const WorldState& state) -> std::vector<std::byte> {
    std::vector<std::byte> bytes;
    bytes.reserve(2'048U);
    append_little_endian(bytes, kSimulationVersion);
    append_little_endian(bytes, state.frame.value);
    for (const auto& player : state.players) {
        append_little_endian(bytes, static_cast<std::uint8_t>(player.id));
        append_little_endian(bytes, player.x);
        append_little_endian(bytes, player.y);
        append_little_endian(bytes, player.velocity_x);
        append_little_endian(bytes, player.velocity_y);
        append_little_endian(bytes, player.facing_x);
        append_little_endian(bytes, player.facing_y);
        append_little_endian(bytes, player.hp);
        append_little_endian(bytes, player.score);
        append_little_endian(bytes, player.attack_cooldown);
    }
    append_little_endian(bytes, state.next_projectile_id);
    for (const auto& projectile : state.projectiles) {
        append_little_endian(bytes,
                             static_cast<std::uint8_t>(projectile.active ? 1U : 0U));
        append_little_endian(bytes, projectile.stable_id);
        append_little_endian(bytes, static_cast<std::uint8_t>(projectile.owner));
        append_little_endian(bytes, projectile.x);
        append_little_endian(bytes, projectile.y);
        append_little_endian(bytes, projectile.velocity_x);
        append_little_endian(bytes, projectile.velocity_y);
        append_little_endian(bytes, projectile.ttl);
    }
    return bytes;
}

auto hash_canonical(const WorldState& state) -> StateHash {
    return fnv1a64(serialize_canonical(state));
}

}  // namespace rollback_lab
