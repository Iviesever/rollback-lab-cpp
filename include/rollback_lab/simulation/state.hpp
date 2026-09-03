#pragma once

#include <rollback_lab/core/frame.hpp>
#include <rollback_lab/simulation/input.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

namespace rollback_lab {

inline constexpr std::int32_t kSubunitsPerWorldUnit = 1'024;
inline constexpr std::int32_t kArenaWidth = 1'024 * kSubunitsPerWorldUnit;
inline constexpr std::int32_t kArenaHeight = 576 * kSubunitsPerWorldUnit;
inline constexpr std::int32_t kPlayerRadius = 12 * kSubunitsPerWorldUnit;
inline constexpr std::int32_t kProjectileRadius = 4 * kSubunitsPerWorldUnit;
inline constexpr std::int32_t kPlayerSpeed = 4 * kSubunitsPerWorldUnit;
inline constexpr std::int32_t kProjectileSpeed = 8 * kSubunitsPerWorldUnit;
inline constexpr std::int32_t kPlayerASpawnX = kArenaWidth / 4;
inline constexpr std::int32_t kPlayerBSpawnX = (kArenaWidth * 3) / 4;
inline constexpr std::int32_t kPlayerSpawnY = kArenaHeight / 2;
inline constexpr std::int16_t kStartingHp = 100;
inline constexpr std::int16_t kProjectileDamage = 25;
inline constexpr std::uint16_t kAttackCooldownTicks = 15U;
inline constexpr std::uint16_t kProjectileLifetimeTicks = 120U;
inline constexpr std::size_t kMaxProjectiles = 64U;

struct PlayerState final {
    PlayerId id{PlayerId::a};
    std::int32_t x{};
    std::int32_t y{};
    std::int32_t velocity_x{};
    std::int32_t velocity_y{};
    std::int8_t facing_x{1};
    std::int8_t facing_y{};
    std::int16_t hp{kStartingHp};
    std::uint16_t score{};
    std::uint16_t attack_cooldown{};

    auto operator==(const PlayerState&) const -> bool = default;
};

struct ProjectileState final {
    bool active{};
    std::uint32_t stable_id{};
    PlayerId owner{PlayerId::a};
    std::int32_t x{};
    std::int32_t y{};
    std::int32_t velocity_x{};
    std::int32_t velocity_y{};
    std::uint16_t ttl{};

    auto operator==(const ProjectileState&) const -> bool = default;
};

struct WorldState final {
    FrameNumber frame{};
    std::array<PlayerState, 2U> players{};
    std::array<ProjectileState, kMaxProjectiles> projectiles{};
    std::uint32_t next_projectile_id{1U};

    auto operator==(const WorldState&) const -> bool = default;
};

[[nodiscard]] auto make_initial_world() -> WorldState;

}  // namespace rollback_lab

