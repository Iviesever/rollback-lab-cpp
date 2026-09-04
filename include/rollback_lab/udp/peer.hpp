#pragma once

#include <rollback_lab/core/error.hpp>
#include <rollback_lab/simulation/input.hpp>
#include <rollback_lab/simulation/simulation.hpp>
#include <rollback_lab/version.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <rollback_lab/netcode/session.hpp>

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
    // Zero preserves the legacy CLI digest. Nonzero selects engine-udp-v1.
    std::uint32_t engine_abi_version{};
    std::uint32_t advertised_abi_version{};
};

enum class PeerPhase : std::uint32_t { handshake, running, confirming, finished, failed };
struct PeerStep final {
    std::uint32_t logical_tick{};
    PeerPhase phase{PeerPhase::handshake};
    bool handshake_complete{};
};
struct PeerCorrection final {
    CorrectionResult result{};
    WorldState before{};
    WorldState after{};
};

// Transport state only; the caller retains sole ownership of the borrowed
// session. A step never sleeps and drains at most 64 immediately ready packets.
class PeerDriver final {
public:
    static auto create(const PeerConfig& config, RollbackSession& session)
        -> Result<std::unique_ptr<PeerDriver>>;
    ~PeerDriver();
    PeerDriver(const PeerDriver&) = delete;
    auto operator=(const PeerDriver&) -> PeerDriver& = delete;
    auto step(std::uint64_t elapsed_milliseconds) -> Result<PeerStep>;
    [[nodiscard]] auto observation() const -> PeerStep;
    [[nodiscard]] auto correction() const -> const PeerCorrection&;
    [[nodiscard]] auto report_json() const -> const std::string&;
    [[nodiscard]] auto replay_bytes() const -> const std::vector<std::byte>&;
    [[nodiscard]] auto failure_json() const -> std::string;
    [[nodiscard]] auto diagnostic_json() const -> Result<std::string>;
private:
    struct Impl;
    explicit PeerDriver(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] auto run_peer(const PeerConfig& config) -> Result<int>;

}  // namespace rollback_lab
