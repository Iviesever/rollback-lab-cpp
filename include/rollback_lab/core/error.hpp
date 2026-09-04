#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <variant>

namespace rollback_lab {

enum class ErrorCode : std::uint16_t {
    none = 0,
    invalid_argument,
    arithmetic_overflow,
    frame_mismatch,
    capacity_exceeded,
    stale_frame,
    rollback_window_exceeded,
    invalid_magic,
    unsupported_version,
    unknown_packet_type,
    truncated_data,
    invalid_length,
    integrity_mismatch,
    duplicate_sequence,
    stale_sequence,
    queue_overflow,
    timeout,
    child_failure,
    replay_mismatch,
    desync,
    io_error,
    internal_failure,
};

struct Error final {
    ErrorCode code{ErrorCode::none};
    std::uint64_t detail{};
    std::size_t offset{};
    const char* context{"none"};

    auto operator==(const Error&) const -> bool = default;
};

template <typename T>
class Result final {
public:
    static auto success(T value) -> Result { return Result{std::move(value)}; }
    static auto failure(const Error error) -> Result { return Result{error}; }

    [[nodiscard]] auto ok() const noexcept -> bool {
        return std::holds_alternative<T>(storage_);
    }

    auto value() & -> T& { return std::get<T>(storage_); }
    auto value() const& -> const T& { return std::get<T>(storage_); }
    auto value() && -> T&& { return std::get<T>(std::move(storage_)); }
    auto error() const -> const Error& { return std::get<Error>(storage_); }

private:
    explicit Result(T value) : storage_(std::move(value)) {}
    explicit Result(const Error error) : storage_(error) {}

    std::variant<T, Error> storage_;
};

template <>
class Result<void> final {
public:
    static auto success() -> Result { return Result{}; }
    static auto failure(const Error error) -> Result { return Result{error}; }

    [[nodiscard]] auto ok() const noexcept -> bool { return ok_; }
    auto error() const -> const Error& { return error_; }

private:
    Result() = default;
    explicit Result(const Error error) : ok_(false), error_(error) {}

    bool ok_{true};
    Error error_{};
};

}  // namespace rollback_lab

