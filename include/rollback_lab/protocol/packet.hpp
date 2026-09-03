#pragma once

#include <rollback_lab/core/hash.hpp>
#include <rollback_lab/simulation/input.hpp>
#include <rollback_lab/version.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace rollback_lab {

inline constexpr std::uint32_t kPacketMagic = 0x4B424C52U;
inline constexpr std::size_t kMaxPacketBytes = 1'200U;
inline constexpr std::size_t kMaxRedundantInputs = 32U;

enum class PacketType : std::uint8_t {
    hello = 1U,
    input = 2U,
    state_hash = 3U,
    goodbye = 4U,
};

struct ConfirmedHash final {
    FrameNumber frame{};
    StateHash hash{};

    auto operator==(const ConfirmedHash&) const -> bool = default;
};

struct Packet final {
    std::uint16_t protocol_version{
        static_cast<std::uint16_t>(kProtocolVersion)};
    PacketType type{PacketType::hello};
    PlayerId sender{PlayerId::a};
    std::uint32_t sequence{};
    std::uint32_t ack{};
    std::uint64_t scenario_id{};
    FrameNumber confirmed_frame{};
    std::vector<InputFrame> inputs;
    std::optional<ConfirmedHash> confirmed_hash;

    auto operator==(const Packet&) const -> bool = default;
};

}  // namespace rollback_lab

