#pragma once

#include <rollback_lab/core/error.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <vector>

namespace rollback_lab {

class ByteWriter final {
public:
    template <typename T>
    void append_little_endian(const T value) {
        static_assert(std::is_integral_v<T>);
        using Unsigned = std::make_unsigned_t<T>;
        const auto bits = static_cast<Unsigned>(value);
        for (std::size_t index = 0; index < sizeof(T); ++index) {
            bytes_.push_back(static_cast<std::byte>(
                (bits >> static_cast<unsigned>(index * 8U)) &
                static_cast<Unsigned>(0xFFU)));
        }
    }

    [[nodiscard]] auto size() const noexcept -> std::size_t {
        return bytes_.size();
    }

    [[nodiscard]] auto bytes() const noexcept
        -> std::span<const std::byte> {
        return bytes_;
    }

    void patch_u16(const std::size_t offset, const std::uint16_t value) {
        bytes_[offset] = static_cast<std::byte>(value & 0xFFU);
        bytes_[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xFFU);
    }

    [[nodiscard]] auto take() -> std::vector<std::byte> {
        return std::move(bytes_);
    }

private:
    std::vector<std::byte> bytes_;
};

class ByteReader final {
public:
    explicit ByteReader(const std::span<const std::byte> bytes) : bytes_(bytes) {}

    template <typename T>
    [[nodiscard]] auto read_little_endian(const char* context) -> Result<T> {
        static_assert(std::is_integral_v<T>);
        if (remaining() < sizeof(T)) {
            return Result<T>::failure(
                Error{ErrorCode::truncated_data, sizeof(T), offset_, context});
        }
        using Unsigned = std::make_unsigned_t<T>;
        Unsigned bits{};
        for (std::size_t index = 0; index < sizeof(T); ++index) {
            bits |= static_cast<Unsigned>(
                        std::to_integer<std::uint8_t>(bytes_[offset_ + index]))
                    << static_cast<unsigned>(index * 8U);
        }
        offset_ += sizeof(T);
        return Result<T>::success(static_cast<T>(bits));
    }

    [[nodiscard]] auto offset() const noexcept -> std::size_t { return offset_; }
    [[nodiscard]] auto remaining() const noexcept -> std::size_t {
        return bytes_.size() - offset_;
    }

private:
    std::span<const std::byte> bytes_;
    std::size_t offset_{};
};

}  // namespace rollback_lab

