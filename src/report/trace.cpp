#include <rollback_lab/report/trace.hpp>

namespace rollback_lab {

auto trace_sample_interval(const std::uint32_t frame_count) noexcept
    -> std::uint32_t {
    constexpr auto sampled_capacity =
        static_cast<std::uint32_t>(kMaxTraceFrames - 1U);
    if (frame_count <= sampled_capacity) {
        return 1U;
    }
    return static_cast<std::uint32_t>(
        (static_cast<std::uint64_t>(frame_count) + sampled_capacity - 1U) /
        sampled_capacity);
}

}  // namespace rollback_lab
