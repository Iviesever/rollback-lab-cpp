#include <rollback_lab/core/hash.hpp>

namespace rollback_lab {

auto fnv1a64(const std::span<const std::byte> bytes) noexcept -> StateHash {
    constexpr StateHash offset_basis = 14695981039346656037ULL;
    constexpr StateHash prime = 1099511628211ULL;
    StateHash hash = offset_basis;
    for (const auto byte : bytes) {
        hash ^= std::to_integer<std::uint8_t>(byte);
        hash *= prime;
    }
    return hash;
}

}  // namespace rollback_lab

