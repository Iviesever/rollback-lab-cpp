#pragma once

#include <rollback_lab/core/error.hpp>
#include <rollback_lab/core/hash.hpp>
#include <rollback_lab/simulation/input.hpp>
#include <rollback_lab/simulation/simulation.hpp>
#include <rollback_lab/version.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace rollback_lab {

inline constexpr std::uint32_t kReplayMagic = 0x50524C52U;
inline constexpr std::size_t kMaxReplayFrames = 1'000'000U;
inline constexpr std::size_t kMaxReplayCheckpoints = 100'000U;

struct ReplayCheckpoint final {
    FrameNumber frame{};
    StateHash hash{};

    auto operator==(const ReplayCheckpoint&) const -> bool = default;
};

struct Replay final {
    std::uint16_t replay_version{static_cast<std::uint16_t>(kReplayVersion)};
    std::uint16_t simulation_version{
        static_cast<std::uint16_t>(kSimulationVersion)};
    std::uint16_t protocol_version{
        static_cast<std::uint16_t>(kProtocolVersion)};
    SimulationVariant variant{SimulationVariant::canonical};
    std::uint64_t scenario_seed{};
    std::uint64_t transport_seed{};
    FrameNumber final_frame{};
    std::vector<InputPair> confirmed_inputs;
    std::vector<ReplayCheckpoint> checkpoints;
    StateHash expected_final_hash{};

    auto operator==(const Replay&) const -> bool = default;
};

struct ReplayVerification final {
    bool success{};
    FrameNumber final_frame{};
    StateHash actual_final_hash{};
};

[[nodiscard]] auto encode_replay(const Replay& replay)
    -> Result<std::vector<std::byte>>;
[[nodiscard]] auto decode_replay(std::span<const std::byte> bytes)
    -> Result<Replay>;
[[nodiscard]] auto verify_replay(const Replay& replay)
    -> Result<ReplayVerification>;

}  // namespace rollback_lab

