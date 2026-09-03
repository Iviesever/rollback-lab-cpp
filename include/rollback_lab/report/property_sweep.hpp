#pragma once

#include <rollback_lab/core/error.hpp>
#include <rollback_lab/core/hash.hpp>

#include <array>
#include <cstdint>
#include <string>

namespace rollback_lab {

struct PropertySweepConfig final {
    std::uint32_t start_seed{};
    std::uint32_t seed_count{100U};
    std::uint32_t repeat_identity_samples{16U};
};

struct PropertySweepResult final {
    std::uint32_t start_seed{};
    std::uint32_t total_seeds{};
    std::uint32_t successful_seeds{};
    std::uint32_t declared_failures{};
    std::uint32_t repeated_identity_samples{};
    std::uint32_t identity_mismatches{};
    std::uint32_t crashes{};
    std::uint32_t deadlocks{};
    std::uint32_t unbounded_failures{};
    std::uint32_t edge_frame_cases{};
    std::uint32_t queue_overflow_failures{};
    std::uint32_t timeout_failures{};
    std::array<std::uint32_t, 4U> loss_rate_scenarios{};
    StateHash identity_digest{};
};

[[nodiscard]] auto run_property_sweep(const PropertySweepConfig& config)
    -> Result<PropertySweepResult>;
[[nodiscard]] auto canonical_json(const PropertySweepResult& result)
    -> std::string;

}  // namespace rollback_lab
