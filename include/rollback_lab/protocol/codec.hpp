#pragma once

#include <rollback_lab/core/error.hpp>
#include <rollback_lab/protocol/packet.hpp>

#include <cstddef>
#include <span>
#include <vector>

namespace rollback_lab {

[[nodiscard]] auto encode_packet(const Packet& packet)
    -> Result<std::vector<std::byte>>;

[[nodiscard]] auto decode_packet(std::span<const std::byte> bytes)
    -> Result<Packet>;

}  // namespace rollback_lab

