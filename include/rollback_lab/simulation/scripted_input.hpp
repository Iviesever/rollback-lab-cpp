#pragma once

#include <rollback_lab/simulation/input.hpp>

#include <cstdint>

namespace rollback_lab {

[[nodiscard]] auto scripted_input(std::uint64_t scenario_seed,
                                  FrameNumber frame,
                                  PlayerId player) -> InputFrame;

}  // namespace rollback_lab

