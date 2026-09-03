#include <rollback_lab/transport/seeded_transport.hpp>

#include <rollback_lab/protocol/packet.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>
#include <vector>

namespace rollback_lab {

struct SeededTransport::Scheduled final {
    Endpoint from{Endpoint::a};
    Endpoint to{Endpoint::b};
    std::vector<std::byte> bytes;
    LogicalTick enqueued_at{};
    LogicalTick scheduled_for{};
    std::uint32_t reorder_rank{};
    std::uint64_t ordinal{};
    bool duplicate{};
};

SeededTransport::SeededTransport(TransportConfig config)
    : config_(std::move(config)), random_(config_.seed) {
    queue_.reserve(config_.max_queue_packets);
}

SeededTransport::~SeededTransport() = default;
SeededTransport::SeededTransport(SeededTransport&&) noexcept = default;
auto SeededTransport::operator=(SeededTransport&&) noexcept
    -> SeededTransport& = default;

auto SeededTransport::chance(const std::uint32_t percent) noexcept -> bool {
    return percent != 0U && random_.bounded(100U) < percent;
}

auto SeededTransport::ensure_capacity(const std::size_t packet_count,
                                      const std::size_t byte_count)
    -> Result<void> {
    auto exceeds = [&]() {
        return queue_.size() + packet_count > config_.max_queue_packets ||
               queued_bytes_ + byte_count > config_.max_queue_bytes;
    };
    if (!exceeds()) {
        return Result<void>::success();
    }
    if (config_.overflow_policy == QueueOverflowPolicy::fail) {
        return Result<void>::failure(
            Error{ErrorCode::queue_overflow, queue_.size(), 0U,
                  "transport_queue"});
    }
    while (exceeds() && !queue_.empty()) {
        queued_bytes_ -= queue_.front().bytes.size();
        queue_.erase(queue_.begin());
        ++metrics_.dropped_overflow;
    }
    if (exceeds()) {
        return Result<void>::failure(
            Error{ErrorCode::queue_overflow, byte_count, 0U,
                  "transport_packet"});
    }
    return Result<void>::success();
}

auto SeededTransport::send(const Endpoint from,
                           const Endpoint to,
                           const std::span<const std::byte> bytes,
                           const LogicalTick now) -> Result<void> {
    ++metrics_.sent_packets;
    if (bytes.size() > kMaxPacketBytes) {
        return Result<void>::failure(
            Error{ErrorCode::invalid_length, bytes.size(), 0U,
                  "transport_packet"});
    }
    if (config_.loss_percent > 100U || config_.reorder_percent > 100U ||
        config_.duplicate_percent > 100U ||
        config_.burst_loss_percent > 100U) {
        return Result<void>::failure(
            Error{ErrorCode::invalid_argument, 100U, 0U,
                  "transport_percent"});
    }

    bool lost = false;
    if (burst_remaining_ > 0U) {
        --burst_remaining_;
        lost = true;
    } else if (chance(config_.burst_loss_percent)) {
        burst_remaining_ = 1U + random_.bounded(3U);
        lost = true;
    } else {
        lost = chance(config_.loss_percent);
    }
    if (lost) {
        ++metrics_.dropped_loss;
        return Result<void>::success();
    }

    const bool duplicate = chance(config_.duplicate_percent);
    const std::size_t copies = duplicate ? 2U : 1U;
    const auto capacity = ensure_capacity(copies, bytes.size() * copies);
    if (!capacity.ok()) {
        return capacity;
    }

    for (std::size_t copy = 0U; copy < copies; ++copy) {
        std::int64_t delay = config_.base_latency_ticks;
        if (config_.jitter_ticks > 0U) {
            const auto width = static_cast<std::uint64_t>(config_.jitter_ticks) *
                                   2ULL +
                               1ULL;
            const auto draw = random_.next_u32() % width;
            delay += static_cast<std::int64_t>(draw) -
                     static_cast<std::int64_t>(config_.jitter_ticks);
        }
        delay = std::max<std::int64_t>(0, delay);
        std::uint32_t reorder_rank{};
        if (chance(config_.reorder_percent)) {
            reorder_rank = 1U +
                           random_.bounded(std::max(2U,
                               config_.jitter_ticks + 2U));
            delay += reorder_rank;
            ++metrics_.reordered_packets;
        }
        const auto scheduled = static_cast<std::uint64_t>(now.value) +
                               static_cast<std::uint64_t>(delay);
        const auto bounded_tick = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(scheduled,
                                    std::numeric_limits<std::uint32_t>::max()));
        queue_.push_back(Scheduled{
            from, to, std::vector<std::byte>{bytes.begin(), bytes.end()}, now,
            LogicalTick{bounded_tick}, reorder_rank, next_ordinal_++, copy != 0U});
        queued_bytes_ += bytes.size();
    }
    if (duplicate) {
        ++metrics_.duplicated_packets;
    }
    return Result<void>::success();
}

auto SeededTransport::deliver(const LogicalTick now) -> std::vector<Delivery> {
    std::stable_sort(queue_.begin(), queue_.end(),
                     [](const Scheduled& left, const Scheduled& right) {
        if (left.scheduled_for.value != right.scheduled_for.value) {
            return left.scheduled_for.value < right.scheduled_for.value;
        }
        if (left.reorder_rank != right.reorder_rank) {
            return left.reorder_rank < right.reorder_rank;
        }
        return left.ordinal < right.ordinal;
    });

    std::vector<Delivery> deliveries;
    std::size_t budget = config_.bandwidth_bytes_per_tick;
    auto iterator = queue_.begin();
    while (iterator != queue_.end()) {
        const auto age = static_cast<std::uint32_t>(
            now.value - iterator->enqueued_at.value);
        if (age > config_.max_packet_age_ticks) {
            queued_bytes_ -= iterator->bytes.size();
            iterator = queue_.erase(iterator);
            ++metrics_.dropped_age;
            continue;
        }
        if (iterator->scheduled_for.value <= now.value &&
            iterator->bytes.size() <= budget) {
            budget -= iterator->bytes.size();
            queued_bytes_ -= iterator->bytes.size();
            deliveries.push_back(Delivery{
                iterator->from, iterator->to, std::move(iterator->bytes),
                iterator->enqueued_at, now, iterator->ordinal,
                iterator->duplicate});
            iterator = queue_.erase(iterator);
            ++metrics_.delivered_packets;
            continue;
        }
        ++iterator;
    }
    return deliveries;
}

auto SeededTransport::metrics() const noexcept -> TransportMetrics {
    return metrics_;
}

auto SeededTransport::queued_packets() const noexcept -> std::size_t {
    return queue_.size();
}

}  // namespace rollback_lab
