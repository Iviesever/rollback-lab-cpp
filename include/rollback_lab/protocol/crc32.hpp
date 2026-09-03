#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace rollback_lab {

// CRC-32/ISO-HDLC detects accidental corruption. It is not authentication.
[[nodiscard]] auto crc32(std::span<const std::byte> bytes) noexcept
    -> std::uint32_t;

}  // namespace rollback_lab

