#pragma once

#include <cstdint>

namespace rollback_lab {

class Pcg32 final {
public:
    static constexpr std::uint32_t algorithm_version = 1U;

    explicit Pcg32(const std::uint64_t seed,
                   const std::uint64_t stream = 0xDA3E39CB94B95BDBULL)
        : increment_((stream << 1U) | 1U) {
        static_cast<void>(next_u32());
        state_ += seed;
        static_cast<void>(next_u32());
    }

    [[nodiscard]] auto next_u32() noexcept -> std::uint32_t {
        const std::uint64_t old_state = state_;
        state_ = old_state * 6364136223846793005ULL + increment_;
        const auto mixed = static_cast<std::uint32_t>(
            ((old_state >> 18U) ^ old_state) >> 27U);
        const auto rotation = static_cast<std::uint32_t>(old_state >> 59U);
        return static_cast<std::uint32_t>(
            (mixed >> rotation) | (mixed << ((0U - rotation) & 31U)));
    }

    [[nodiscard]] auto bounded(const std::uint32_t bound) noexcept
        -> std::uint32_t {
        if (bound == 0U) {
            return 0U;
        }
        const auto threshold = static_cast<std::uint32_t>(0U - bound) % bound;
        for (;;) {
            const auto value = next_u32();
            if (value >= threshold) {
                return value % bound;
            }
        }
    }

private:
    std::uint64_t state_{};
    std::uint64_t increment_{};
};

}  // namespace rollback_lab

