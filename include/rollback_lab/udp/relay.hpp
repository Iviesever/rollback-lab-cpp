#pragma once

#include <rollback_lab/core/error.hpp>

#include <cstdint>
#include <filesystem>

namespace rollback_lab {

struct RelayConfig final {
    std::uint16_t relay_port{};
    std::uint16_t peer_a_port{};
    std::uint16_t peer_b_port{};
    std::filesystem::path ready_file;
    std::uint32_t maximum_runtime_milliseconds{5'000U};
};

[[nodiscard]] auto run_relay(const RelayConfig& config) -> Result<int>;

}  // namespace rollback_lab

