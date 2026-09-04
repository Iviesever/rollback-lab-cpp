#pragma once

#include <rollback_lab/core/error.hpp>
#include <rollback_lab/core/pcg32.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace rollback_lab {

struct LogicalTick final {
    std::uint32_t value{};
    auto operator==(const LogicalTick&) const -> bool = default;
};

enum class Endpoint : std::uint8_t { a = 0U, b = 1U, relay = 2U };
enum class QueueOverflowPolicy : std::uint8_t { fail, drop_oldest };

struct TransportConfig final {
    std::uint64_t seed{1U};
    std::uint32_t base_latency_ticks{};
    std::uint32_t jitter_ticks{};
    std::uint32_t loss_percent{};
    std::uint32_t reorder_percent{};
    std::uint32_t duplicate_percent{};
    std::uint32_t burst_loss_percent{};
    std::size_t max_queue_packets{1'024U};
    std::size_t max_queue_bytes{1U << 20U};
    std::size_t bandwidth_bytes_per_tick{1U << 20U};
    std::uint32_t max_packet_age_ticks{600U};
    QueueOverflowPolicy overflow_policy{QueueOverflowPolicy::fail};
};

struct Delivery final {
    Endpoint from{Endpoint::a};
    Endpoint to{Endpoint::b};
    std::vector<std::byte> bytes;
    LogicalTick enqueued_at{};
    LogicalTick delivered_at{};
    std::uint64_t ordinal{};
    bool duplicate{};

    auto operator==(const Delivery&) const -> bool = default;
};

struct TransportMetrics final {
    std::uint64_t sent_packets{};
    std::uint64_t delivered_packets{};
    std::uint64_t dropped_loss{};
    std::uint64_t dropped_overflow{};
    std::uint64_t dropped_age{};
    std::uint64_t duplicated_packets{};
    std::uint64_t reordered_packets{};

    auto operator==(const TransportMetrics&) const -> bool = default;
};

class SeededTransport final {
public:
    explicit SeededTransport(TransportConfig config);
    ~SeededTransport();

    SeededTransport(const SeededTransport&) = delete;
    auto operator=(const SeededTransport&) -> SeededTransport& = delete;
    SeededTransport(SeededTransport&&) noexcept;
    auto operator=(SeededTransport&&) noexcept -> SeededTransport&;

    [[nodiscard]] auto send(Endpoint from,
                            Endpoint to,
                            std::span<const std::byte> bytes,
                            LogicalTick now) -> Result<void>;
    [[nodiscard]] auto deliver(LogicalTick now) -> std::vector<Delivery>;
    [[nodiscard]] auto metrics() const noexcept -> TransportMetrics;
    [[nodiscard]] auto queued_packets() const noexcept -> std::size_t;

private:
    struct Scheduled;

    [[nodiscard]] auto chance(std::uint32_t percent) noexcept -> bool;
    [[nodiscard]] auto ensure_capacity(std::size_t packet_count,
                                       std::size_t byte_count) -> Result<void>;

    TransportConfig config_;
    Pcg32 random_;
    std::vector<Scheduled> queue_;
    std::size_t queued_bytes_{};
    std::uint64_t next_ordinal_{};
    std::uint32_t burst_remaining_{};
    TransportMetrics metrics_{};
};

}  // namespace rollback_lab
