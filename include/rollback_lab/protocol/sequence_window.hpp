#pragma once

#include <cstdint>

namespace rollback_lab {

enum class SequenceDisposition : std::uint8_t {
    newest,
    out_of_order,
    duplicate,
    stale,
};

class SequenceWindow final {
public:
    [[nodiscard]] auto observe(std::uint32_t sequence) noexcept
        -> SequenceDisposition;

private:
    bool initialized_{};
    std::uint32_t newest_{};
    std::uint64_t received_mask_{};
};

}  // namespace rollback_lab

