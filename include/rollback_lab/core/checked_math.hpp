#pragma once

#include <rollback_lab/core/error.hpp>

#include <cstdint>
#include <limits>

namespace rollback_lab {

[[nodiscard]] inline auto checked_add_i32(const std::int32_t left,
                                          const std::int32_t right)
    -> Result<std::int32_t> {
    const auto wide = static_cast<std::int64_t>(left) +
                      static_cast<std::int64_t>(right);
    if (wide < std::numeric_limits<std::int32_t>::min() ||
        wide > std::numeric_limits<std::int32_t>::max()) {
        return Result<std::int32_t>::failure(
            Error{ErrorCode::arithmetic_overflow, 0U, 0U, "checked_add_i32"});
    }
    return Result<std::int32_t>::success(static_cast<std::int32_t>(wide));
}

}  // namespace rollback_lab

