#include <rollback_lab/protocol/sequence_window.hpp>

namespace rollback_lab {

auto SequenceWindow::observe(const std::uint32_t sequence) noexcept
    -> SequenceDisposition {
    if (!initialized_) {
        initialized_ = true;
        newest_ = sequence;
        received_mask_ = 1U;
        return SequenceDisposition::newest;
    }

    const auto forward = static_cast<std::uint32_t>(sequence - newest_);
    if (forward == 0U) {
        return SequenceDisposition::duplicate;
    }
    if (forward < 0x80000000U) {
        received_mask_ = forward >= 64U
                             ? 1U
                             : static_cast<std::uint64_t>(
                                   (received_mask_ << forward) | 1U);
        newest_ = sequence;
        return SequenceDisposition::newest;
    }

    const auto behind = static_cast<std::uint32_t>(newest_ - sequence);
    if (behind >= 64U) {
        return SequenceDisposition::stale;
    }
    const auto bit = static_cast<std::uint64_t>(1ULL << behind);
    if ((received_mask_ & bit) != 0U) {
        return SequenceDisposition::duplicate;
    }
    received_mask_ |= bit;
    return SequenceDisposition::out_of_order;
}

}  // namespace rollback_lab

