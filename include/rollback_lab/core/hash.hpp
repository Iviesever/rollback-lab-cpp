#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace rollback_lab {

using StateHash = std::uint64_t;

[[nodiscard]] auto fnv1a64(std::span<const std::byte> bytes) noexcept
    -> StateHash;

}  // namespace rollback_lab

