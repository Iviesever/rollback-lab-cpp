#pragma once

#include <rollback_lab/core/frame.hpp>

#include <cstdint>

namespace rollback_lab {

enum class PlayerId : std::uint8_t { a = 0U, b = 1U };

enum class Button : std::uint8_t {
    up = 0U,
    down = 1U,
    left = 2U,
    right = 3U,
    attack = 4U,
};

[[nodiscard]] constexpr auto button_mask(const Button button) noexcept
    -> std::uint8_t {
    return static_cast<std::uint8_t>(1U << static_cast<std::uint8_t>(button));
}

[[nodiscard]] constexpr auto has_button(const std::uint8_t buttons,
                                        const Button button) noexcept -> bool {
    return (buttons & button_mask(button)) != 0U;
}

struct InputFrame final {
    FrameNumber frame{};
    PlayerId player{PlayerId::a};
    std::uint32_t sequence{};
    std::uint8_t buttons{};

    auto operator==(const InputFrame&) const -> bool = default;
};

struct InputPair final {
    InputFrame a{};
    InputFrame b{};

    auto operator==(const InputPair&) const -> bool = default;
};

}  // namespace rollback_lab

