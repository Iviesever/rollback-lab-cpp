#pragma once

#include <rollback_lab/core/error.hpp>
#include <rollback_lab/core/hash.hpp>
#include <rollback_lab/simulation/state.hpp>
#include <rollback_lab/transport/seeded_transport.hpp>
#include <rollback_lab/version.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace rollback_lab {

inline constexpr std::size_t kMaxTraceFrames = 10'000U;
inline constexpr std::size_t kMaxTracePacketEvents = 40'000U;
inline constexpr std::size_t kMaxTraceRollbackEvents = 10'000U;

enum class PacketTraceKind : std::uint8_t {
    sent,
    delivered,
    dropped,
    duplicated,
    reordered,
};

struct TraceFrame final {
    FrameNumber frame{};
    WorldState state{};
    StateHash hash{};
    bool predicted{};
    FrameNumber confirmed_frame{};
};

struct PacketTraceEvent final {
    LogicalTick tick{};
    PacketTraceKind kind{PacketTraceKind::sent};
    Endpoint from{Endpoint::a};
    Endpoint to{Endpoint::b};
    std::uint32_t sequence{};
};

struct RollbackTraceEvent final {
    FrameNumber observed_at{};
    FrameNumber rollback_from{};
    std::uint32_t depth{};
};

struct TraceDesyncMarker final {
    FrameNumber frame{};
    StateHash local_hash{};
    StateHash remote_hash{};
};

struct Trace final {
    std::uint32_t trace_version{kTraceVersion};
    std::uint64_t scenario_seed{};
    std::uint32_t sample_interval{1U};
    std::vector<TraceFrame> frames;
    std::vector<PacketTraceEvent> packets;
    std::vector<RollbackTraceEvent> rollbacks;
    std::optional<TraceDesyncMarker> desync;
};

}  // namespace rollback_lab

