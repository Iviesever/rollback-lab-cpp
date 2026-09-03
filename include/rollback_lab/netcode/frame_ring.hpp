#pragma once

#include <rollback_lab/core/error.hpp>
#include <rollback_lab/core/frame.hpp>

#include <array>
#include <cstddef>
#include <utility>

namespace rollback_lab {

template <typename T, std::size_t Capacity>
class FrameRing final {
    static_assert(Capacity > 0U);

public:
    [[nodiscard]] auto put(const FrameNumber frame, T value) -> Result<void> {
        auto& slot = slots_[frame_ring_index(frame, Capacity)];
        slot.frame = frame;
        slot.value = std::move(value);
        slot.valid = true;
        return Result<void>::success();
    }

    [[nodiscard]] auto get(const FrameNumber frame) const -> Result<T> {
        const auto& slot = slots_[frame_ring_index(frame, Capacity)];
        if (!slot.valid || slot.frame != frame) {
            return Result<T>::failure(
                Error{ErrorCode::stale_frame, frame.value, 0U, "frame_ring"});
        }
        return Result<T>::success(slot.value);
    }

    [[nodiscard]] auto contains(const FrameNumber frame) const noexcept -> bool {
        const auto& slot = slots_[frame_ring_index(frame, Capacity)];
        return slot.valid && slot.frame == frame;
    }

private:
    struct Slot final {
        bool valid{};
        FrameNumber frame{};
        T value{};
    };

    std::array<Slot, Capacity> slots_{};
};

}  // namespace rollback_lab

