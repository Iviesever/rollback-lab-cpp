#pragma once

#include <rollback_lab/core/hash.hpp>
#include <rollback_lab/simulation/input.hpp>
#include <rollback_lab/simulation/state.hpp>
#include <rollback_lab/version.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace rollback_lab {

struct HashObservation final {
    FrameNumber frame{};
    StateHash hash{};
    bool confirmed{};
};

struct PlayerStateSummary final {
    PlayerId id{PlayerId::a};
    std::int32_t x{};
    std::int32_t y{};
    std::int16_t hp{};
    std::uint16_t score{};
};

struct DesyncDiagnostic final {
    FrameNumber earliest_divergent_frame{};
    StateHash local_hash{};
    StateHash remote_hash{};
    std::vector<InputPair> recent_inputs;
    std::array<PlayerStateSummary, 2U> players{};
    std::uint32_t active_projectiles{};
    std::uint32_t simulation_version{kSimulationVersion};
    std::uint32_t protocol_version{kProtocolVersion};
    std::uint64_t scenario_seed{};
};

class DesyncTracker final {
public:
    explicit DesyncTracker(std::uint64_t scenario_seed);

    [[nodiscard]] auto observe(const HashObservation& local,
                               const HashObservation& remote,
                               std::vector<InputPair> recent_inputs,
                               const WorldState& local_state)
        -> std::optional<DesyncDiagnostic>;
    [[nodiscard]] auto diagnostic() const
        -> const std::optional<DesyncDiagnostic>&;

private:
    std::uint64_t scenario_seed_{};
    std::optional<DesyncDiagnostic> diagnostic_;
};

}  // namespace rollback_lab
