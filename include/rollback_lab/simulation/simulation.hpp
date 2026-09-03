#pragma once

#include <rollback_lab/core/error.hpp>
#include <rollback_lab/core/hash.hpp>
#include <rollback_lab/simulation/input.hpp>
#include <rollback_lab/simulation/state.hpp>

#include <cstddef>
#include <vector>

namespace rollback_lab {

enum class SimulationVariant : std::uint8_t {
    canonical = 0U,
    damage_bias = 1U,
};

[[nodiscard]] auto simulate_frame(
    const WorldState& before,
    FrameNumber frame,
    const InputPair& inputs,
    SimulationVariant variant = SimulationVariant::canonical)
    -> Result<WorldState>;

[[nodiscard]] auto serialize_canonical(const WorldState& state)
    -> std::vector<std::byte>;

[[nodiscard]] auto hash_canonical(const WorldState& state) -> StateHash;

}  // namespace rollback_lab

