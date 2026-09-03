#pragma once

#include <rollback_lab/core/error.hpp>
#include <rollback_lab/replay/replay.hpp>
#include <rollback_lab/report/run_report.hpp>
#include <rollback_lab/report/trace.hpp>
#include <rollback_lab/transport/seeded_transport.hpp>

#include <cstdint>

namespace rollback_lab {

struct ScenarioRunConfig final {
    std::uint64_t scenario_seed{1U};
    std::uint64_t transport_seed{2U};
    std::uint32_t frame_count{600U};
    TransportConfig transport{};
    SimulationVariant peer_b_variant{SimulationVariant::canonical};
    bool capture_trace{true};
    std::uint32_t tail_redundancy_ticks{64U};
};

struct ScenarioArtifacts final {
    RunReport report{};
    Replay replay{};
    Trace trace{};
};

[[nodiscard]] auto run_seeded_scenario(const ScenarioRunConfig& config)
    -> Result<ScenarioArtifacts>;

}  // namespace rollback_lab
