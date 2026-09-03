#pragma once

#include <rollback_lab/core/error.hpp>
#include <rollback_lab/simulation/input.hpp>
#include <rollback_lab/simulation/simulation.hpp>
#include <rollback_lab/version.hpp>

#include <cstdint>
#include <filesystem>

namespace rollback_lab {

struct PeerConfig final {
    PlayerId id{PlayerId::a};
    std::uint16_t listen_port{};
    std::uint16_t relay_port{};
    std::uint64_t scenario_seed{1U};
    std::uint64_t transport_seed{2U};
    std::uint32_t frame_count{120U};
    std::uint16_t protocol_version_override{1U};
    std::uint32_t simulation_version_override{kSimulationVersion};
    SimulationVariant simulation_variant{SimulationVariant::canonical};
    std::uint32_t handshake_timeout_milliseconds{1'000U};
    std::uint32_t run_timeout_milliseconds{4'000U};
    std::filesystem::path report_path;
    std::filesystem::path replay_path;
    std::filesystem::path diagnostic_path;
};

[[nodiscard]] auto run_peer(const PeerConfig& config) -> Result<int>;

}  // namespace rollback_lab
