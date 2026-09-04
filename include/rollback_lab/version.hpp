#pragma once

#include <cstdint>
#include <string_view>

#ifndef ROLLBACK_LAB_GIT_SHA
#define ROLLBACK_LAB_GIT_SHA "unavailable"
#endif

namespace rollback_lab {

inline constexpr std::uint32_t kSimulationVersion = 1U;
inline constexpr std::uint32_t kProtocolVersion = 1U;
inline constexpr std::uint32_t kReplayVersion = 1U;
inline constexpr std::uint32_t kTraceVersion = 1U;
inline constexpr std::string_view kGitSha = ROLLBACK_LAB_GIT_SHA;

}  // namespace rollback_lab
