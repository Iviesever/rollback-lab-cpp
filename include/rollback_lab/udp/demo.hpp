#pragma once

#include <rollback_lab/core/error.hpp>
#include <rollback_lab/core/frame.hpp>
#include <rollback_lab/core/hash.hpp>
#include <rollback_lab/version.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>

namespace rollback_lab {

struct UdpDemoConfig final {
    std::filesystem::path executable_path;
    std::filesystem::path output_directory{"artifacts/udp-demo"};
    std::uint64_t scenario_seed{0xC0FFEEU};
    std::uint64_t transport_seed{0x55445030ULL};
    std::uint32_t frame_count{120U};
    std::uint32_t watchdog_milliseconds{5'000U};
    std::optional<std::uint16_t> forced_relay_port;
    bool launch_peer_b{true};
    std::uint16_t peer_b_protocol_version{1U};
    std::uint32_t peer_b_simulation_version{kSimulationVersion};
};

struct UdpDemoResult final {
    std::uint32_t relay_pid{};
    std::uint32_t peer_a_pid{};
    std::uint32_t peer_b_pid{};
    std::uint16_t relay_port{};
    std::uint16_t peer_a_port{};
    std::uint16_t peer_b_port{};
    FrameNumber confirmed_frame{};
    StateHash final_hash_a{};
    StateHash final_hash_b{};
    bool replay_verified{};
    int relay_exit_code{};
    int peer_a_exit_code{};
    int peer_b_exit_code{};
};

[[nodiscard]] auto run_udp_demo(const UdpDemoConfig& config)
    -> Result<UdpDemoResult>;

}  // namespace rollback_lab
