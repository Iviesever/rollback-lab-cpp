#pragma once

#include <rollback_lab/core/frame.hpp>

#include <cstdint>

namespace rollback_lab {

struct RollbackMetrics final {
    std::uint32_t rollback_count{};
    std::uint64_t total_resimulated_frames{};
    std::uint32_t maximum_rollback_depth{};
    std::uint64_t predicted_input_count{};
    FrameNumber confirmed_frame{};
    std::uint64_t late_input_count{};

    auto operator==(const RollbackMetrics&) const -> bool = default;
};

}  // namespace rollback_lab

