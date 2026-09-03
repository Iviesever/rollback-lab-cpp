#pragma once

#include <cstddef>
#include <cstdint>

namespace rollback_lab {

struct FrameNumber final {
    std::uint32_t value{};

    auto operator==(const FrameNumber&) const -> bool = default;
};

[[nodiscard]] constexpr auto frame_before(const FrameNumber left,
                                          const FrameNumber right) noexcept
    -> bool {
    const auto distance = static_cast<std::uint32_t>(right.value - left.value);
    return distance != 0U && distance < 0x80000000U;
}

[[nodiscard]] constexpr auto frame_distance(const FrameNumber from,
                                            const FrameNumber to) noexcept
    -> std::uint32_t {
    return static_cast<std::uint32_t>(to.value - from.value);
}

[[nodiscard]] constexpr auto next_frame(const FrameNumber frame) noexcept
    -> FrameNumber {
    return FrameNumber{static_cast<std::uint32_t>(frame.value + 1U)};
}

[[nodiscard]] constexpr auto frame_ring_index(const FrameNumber frame,
                                              const std::size_t capacity) noexcept
    -> std::size_t {
    return capacity == 0U ? 0U : static_cast<std::size_t>(frame.value) % capacity;
}

}  // namespace rollback_lab

