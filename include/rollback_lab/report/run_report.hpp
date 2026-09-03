#pragma once

#include <rollback_lab/core/frame.hpp>
#include <rollback_lab/core/hash.hpp>
#include <rollback_lab/core/pcg32.hpp>
#include <rollback_lab/netcode/metrics.hpp>
#include <rollback_lab/transport/seeded_transport.hpp>
#include <rollback_lab/version.hpp>

#include <cstdint>
#include <string>

namespace rollback_lab {

struct TimingObservations final {
    std::uint64_t simulation_microseconds{};
    std::uint64_t rollback_microseconds{};
};

struct RunReport final {
    std::string git_sha{std::string{kGitSha}};
    std::string build_type{"unknown"};
    std::string compiler{"unknown"};
    std::string os{"unknown"};
    std::uint32_t simulation_version{kSimulationVersion};
    std::uint32_t protocol_version{kProtocolVersion};
    std::uint32_t pcg32_version{Pcg32::algorithm_version};
    std::uint64_t scenario_seed{};
    std::uint64_t transport_seed{};
    std::uint32_t frame_count{};
    TransportConfig transport_config{};
    TransportMetrics transport_metrics{};
    std::uint64_t rollback_count{};
    std::uint64_t resimulated_frames{};
    std::uint32_t maximum_rollback_depth{};
    std::uint64_t predicted_input_count{};
    std::uint64_t late_input_count{};
    FrameNumber confirmed_frame{};
    StateHash final_hash_a{};
    StateHash final_hash_b{};
    bool replay_verified{};
    std::string desync_result{"none"};
    TimingObservations timing{};
    bool success{};
    std::string failure_reason;
};

[[nodiscard]] auto report_identity(const RunReport& report) -> StateHash;

}  // namespace rollback_lab
