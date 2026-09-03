#include <rollback_lab/protocol/crc32.hpp>

namespace rollback_lab {

auto crc32(const std::span<const std::byte> bytes) noexcept -> std::uint32_t {
    std::uint32_t checksum = 0xFFFFFFFFU;
    for (const auto byte : bytes) {
        checksum ^= std::to_integer<std::uint8_t>(byte);
        for (std::uint32_t bit = 0U; bit < 8U; ++bit) {
            const auto mask = static_cast<std::uint32_t>(
                0U - static_cast<std::uint32_t>(checksum & 1U));
            checksum = (checksum >> 1U) ^ (0xEDB88320U & mask);
        }
    }
    return checksum ^ 0xFFFFFFFFU;
}

}  // namespace rollback_lab

