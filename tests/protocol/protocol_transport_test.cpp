#include "test_framework.hpp"

#include <rollback_lab/core/pcg32.hpp>
#include <rollback_lab/protocol/codec.hpp>
#include <rollback_lab/protocol/crc32.hpp>
#include <rollback_lab/protocol/sequence_window.hpp>
#include <rollback_lab/transport/seeded_transport.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace {

using namespace rollback_lab;

auto sample_packet() -> Packet {
    Packet packet{};
    packet.type = PacketType::input;
    packet.sender = PlayerId::a;
    packet.sequence = 77U;
    packet.ack = 63U;
    packet.scenario_id = 0x0102030405060708ULL;
    packet.confirmed_frame = FrameNumber{40U};
    packet.inputs = {
        InputFrame{FrameNumber{42U}, PlayerId::a, 42U,
                   button_mask(Button::left)},
        InputFrame{FrameNumber{43U}, PlayerId::a, 43U,
                   button_mask(Button::right) | button_mask(Button::attack)},
        InputFrame{FrameNumber{44U}, PlayerId::a, 44U,
                   button_mask(Button::up)},
    };
    packet.confirmed_hashes = {
        ConfirmedHash{FrameNumber{39U}, 0x0102030405060708ULL},
        ConfirmedHash{FrameNumber{40U}, 0x1122334455667788ULL},
    };
    return packet;
}

void rewrite_crc(std::vector<std::byte>& bytes) {
    const auto checksum = crc32(std::span<const std::byte>{bytes}.first(bytes.size() - 4U));
    for (std::size_t index = 0; index < 4U; ++index) {
        bytes[bytes.size() - 4U + index] = static_cast<std::byte>(
            (checksum >> static_cast<unsigned>(index * 8U)) & 0xFFU);
    }
}

auto one_byte(const std::uint8_t value) -> std::vector<std::byte> {
    return {static_cast<std::byte>(value)};
}

auto drain(SeededTransport& transport, const std::uint32_t last_tick)
    -> std::vector<Delivery> {
    std::vector<Delivery> deliveries;
    for (std::uint32_t tick = 0U; tick <= last_tick; ++tick) {
        auto batch = transport.deliver(LogicalTick{tick});
        deliveries.insert(deliveries.end(), batch.begin(), batch.end());
    }
    return deliveries;
}

}  // namespace

RL_TEST(crc32_matches_iso_hdlc_check_vector) {
    constexpr std::array bytes{
        std::byte{'1'}, std::byte{'2'}, std::byte{'3'},
        std::byte{'4'}, std::byte{'5'}, std::byte{'6'},
        std::byte{'7'}, std::byte{'8'}, std::byte{'9'},
    };
    RL_CHECK(crc32(bytes) == 0xCBF43926U);
}

RL_TEST(packet_codec_round_trips_all_fields) {
    const auto packet = sample_packet();
    const auto encoded = encode_packet(packet);
    RL_REQUIRE(encoded.ok());
    RL_CHECK(encoded.value().size() <= kMaxPacketBytes);
    RL_CHECK(std::to_integer<std::uint8_t>(encoded.value()[0]) == 'R');
    RL_CHECK(std::to_integer<std::uint8_t>(encoded.value()[1]) == 'L');
    RL_CHECK(std::to_integer<std::uint8_t>(encoded.value()[2]) == 'B');
    RL_CHECK(std::to_integer<std::uint8_t>(encoded.value()[3]) == 'K');
    const auto decoded = decode_packet(encoded.value());
    RL_REQUIRE(decoded.ok());
    RL_CHECK(decoded.value() == packet);
}

RL_TEST(packet_codec_round_trips_every_packet_type) {
    for (const auto type : {PacketType::hello, PacketType::input,
                            PacketType::state_hash, PacketType::goodbye}) {
        auto packet = sample_packet();
        packet.type = type;
        if (type != PacketType::input) {
            packet.inputs.clear();
        }
        if (type == PacketType::hello) {
            packet.confirmed_hashes.clear();
            packet.hello = HelloInfo{kSimulationVersion,
                                     0xAABBCCDDEEFF0011ULL, 120U};
        } else if (type == PacketType::goodbye) {
            packet.confirmed_hashes.clear();
            packet.hello.reset();
        } else {
            packet.hello.reset();
        }
        const auto encoded = encode_packet(packet);
        RL_REQUIRE(encoded.ok());
        const auto decoded = decode_packet(encoded.value());
        RL_REQUIRE(decoded.ok());
        RL_CHECK(decoded.value() == packet);
    }
}

RL_TEST(packet_codec_rejects_every_truncation_boundary) {
    const auto encoded = encode_packet(sample_packet());
    RL_REQUIRE(encoded.ok());
    for (std::size_t size = 0; size < encoded.value().size(); ++size) {
        const auto result = decode_packet(
            std::span<const std::byte>{encoded.value()}.first(size));
        RL_CHECK(!result.ok());
    }
}

RL_TEST(packet_codec_fails_closed_for_header_and_integrity_errors) {
    const auto encoded = encode_packet(sample_packet());
    RL_REQUIRE(encoded.ok());

    auto bad_magic = encoded.value();
    bad_magic[0] = std::byte{0U};
    rewrite_crc(bad_magic);
    RL_CHECK(decode_packet(bad_magic).error().code == ErrorCode::invalid_magic);

    auto bad_version = encoded.value();
    bad_version[4] = std::byte{99U};
    rewrite_crc(bad_version);
    RL_CHECK(decode_packet(bad_version).error().code ==
             ErrorCode::unsupported_version);

    auto bad_type = encoded.value();
    bad_type[6] = std::byte{99U};
    rewrite_crc(bad_type);
    RL_CHECK(decode_packet(bad_type).error().code ==
             ErrorCode::unknown_packet_type);

    auto excess_count = encoded.value();
    excess_count[28] = static_cast<std::byte>(kMaxRedundantInputs + 1U);
    rewrite_crc(excess_count);
    RL_CHECK(decode_packet(excess_count).error().code ==
             ErrorCode::capacity_exceeded);

    auto excess_hash_count = encoded.value();
    excess_hash_count[29] = static_cast<std::byte>(kMaxConfirmedHashes + 1U);
    rewrite_crc(excess_hash_count);
    RL_CHECK(decode_packet(excess_hash_count).error().code ==
             ErrorCode::capacity_exceeded);

    auto bad_length = encoded.value();
    bad_length[30] = std::byte{0U};
    bad_length[31] = std::byte{0U};
    rewrite_crc(bad_length);
    RL_CHECK(decode_packet(bad_length).error().code == ErrorCode::invalid_length);

    auto corrupted = encoded.value();
    corrupted[35] ^= std::byte{0x80U};
    RL_CHECK(decode_packet(corrupted).error().code ==
             ErrorCode::integrity_mismatch);
}

RL_TEST(hello_packet_requires_versioned_scenario_contract) {
    Packet hello{};
    hello.type = PacketType::hello;
    const auto missing = encode_packet(hello);
    RL_CHECK(!missing.ok());
    RL_CHECK(missing.error().code == ErrorCode::invalid_argument);

    hello.hello = HelloInfo{kSimulationVersion, 0x123456789ABCDEF0ULL, 240U};
    const auto encoded = encode_packet(hello);
    RL_REQUIRE(encoded.ok());
    const auto decoded = decode_packet(encoded.value());
    RL_REQUIRE(decoded.ok());
    RL_CHECK(decoded.value().hello == hello.hello);
}

RL_TEST(packet_encoder_rejects_oversized_input_window) {
    auto packet = sample_packet();
    packet.inputs.resize(kMaxRedundantInputs + 1U);
    const auto encoded = encode_packet(packet);
    RL_CHECK(!encoded.ok());
    RL_CHECK(encoded.error().code == ErrorCode::capacity_exceeded);
}

RL_TEST(sequence_window_handles_duplicate_out_of_order_stale_and_wrap) {
    SequenceWindow window;
    RL_CHECK(window.observe(100U) == SequenceDisposition::newest);
    RL_CHECK(window.observe(100U) == SequenceDisposition::duplicate);
    RL_CHECK(window.observe(103U) == SequenceDisposition::newest);
    RL_CHECK(window.observe(101U) == SequenceDisposition::out_of_order);
    RL_CHECK(window.observe(101U) == SequenceDisposition::duplicate);
    RL_CHECK(window.observe(1U) == SequenceDisposition::stale);

    SequenceWindow wrap;
    RL_CHECK(wrap.observe(0xFFFFFFFEU) == SequenceDisposition::newest);
    RL_CHECK(wrap.observe(1U) == SequenceDisposition::newest);
    RL_CHECK(wrap.observe(0xFFFFFFFFU) == SequenceDisposition::out_of_order);
}

RL_TEST(seeded_transport_repeats_identical_schedule_and_metrics) {
    TransportConfig config{};
    config.seed = 12345U;
    config.base_latency_ticks = 5U;
    config.jitter_ticks = 3U;
    config.loss_percent = 5U;
    config.reorder_percent = 20U;
    config.duplicate_percent = 15U;
    config.burst_loss_percent = 3U;
    config.max_queue_packets = 512U;
    config.max_queue_bytes = 4'096U;

    auto run = [&config]() {
        SeededTransport transport{config};
        for (std::uint32_t index = 0; index < 200U; ++index) {
            const auto payload = one_byte(static_cast<std::uint8_t>(index));
            RL_REQUIRE(transport.send(Endpoint::a, Endpoint::b, payload,
                                      LogicalTick{index / 4U}).ok());
        }
        return std::pair{drain(transport, 100U), transport.metrics()};
    };

    const auto first = run();
    const auto second = run();
    RL_CHECK(first == second);
    RL_CHECK(first.second.sent_packets == 200U);
    RL_CHECK(first.second.dropped_loss > 0U);
    RL_CHECK(first.second.duplicated_packets > 0U);
    RL_CHECK(first.second.reordered_packets > 0U);
}

RL_TEST(transport_loss_rates_are_seeded_and_declared) {
    for (const std::uint32_t loss : {0U, 1U, 5U, 20U}) {
        TransportConfig config{};
        config.seed = 9000U + loss;
        config.loss_percent = loss;
        config.max_queue_packets = 2'500U;
        config.max_queue_bytes = 10'000U;
        SeededTransport transport{config};
        for (std::uint32_t index = 0; index < 2'000U; ++index) {
            RL_REQUIRE(transport.send(Endpoint::a, Endpoint::b, one_byte(1U),
                                      LogicalTick{0U}).ok());
        }
        static_cast<void>(drain(transport, 1U));
        RL_CHECK((loss == 0U) == (transport.metrics().dropped_loss == 0U));
    }
}

RL_TEST(transport_burst_loss_drops_a_bounded_run) {
    TransportConfig config{};
    config.seed = 0xB01257U;
    config.loss_percent = 0U;
    config.burst_loss_percent = 100U;
    SeededTransport transport{config};
    for (std::uint32_t packet = 0U; packet < 4U; ++packet) {
        RL_REQUIRE(transport.send(Endpoint::a, Endpoint::b, one_byte(1U),
                                  LogicalTick{packet}).ok());
    }
    RL_CHECK(transport.metrics().dropped_loss == 4U);
    RL_CHECK(transport.queued_packets() == 0U);
}

RL_TEST(transport_queue_overflow_and_packet_age_are_bounded) {
    TransportConfig overflow_config{};
    overflow_config.seed = 1U;
    overflow_config.base_latency_ticks = 20U;
    overflow_config.max_queue_packets = 2U;
    overflow_config.max_queue_bytes = 2U;
    overflow_config.overflow_policy = QueueOverflowPolicy::fail;
    SeededTransport overflow{overflow_config};
    RL_REQUIRE(overflow.send(Endpoint::a, Endpoint::b, one_byte(1U),
                             LogicalTick{0U}).ok());
    RL_REQUIRE(overflow.send(Endpoint::a, Endpoint::b, one_byte(2U),
                             LogicalTick{0U}).ok());
    const auto third = overflow.send(Endpoint::a, Endpoint::b, one_byte(3U),
                                     LogicalTick{0U});
    RL_CHECK(!third.ok());
    RL_CHECK(third.error().code == ErrorCode::queue_overflow);

    TransportConfig age_config{};
    age_config.seed = 2U;
    age_config.base_latency_ticks = 10U;
    age_config.max_packet_age_ticks = 5U;
    SeededTransport aged{age_config};
    RL_REQUIRE(aged.send(Endpoint::a, Endpoint::b, one_byte(1U),
                         LogicalTick{0U}).ok());
    RL_CHECK(aged.deliver(LogicalTick{10U}).empty());
    RL_CHECK(aged.metrics().dropped_age == 1U);
}

RL_TEST(drop_oldest_policy_uses_enqueue_order_after_delivery_sort) {
    std::uint64_t inversion_seed = 0U;
    std::uint32_t first_delivery_tick = 0U;
    for (std::uint64_t seed = 1U; seed <= 1'000U; ++seed) {
        TransportConfig probe_config{};
        probe_config.seed = seed;
        probe_config.base_latency_ticks = 10U;
        probe_config.jitter_ticks = 4U;
        probe_config.reorder_percent = 100U;
        SeededTransport probe{probe_config};
        RL_REQUIRE(probe.send(Endpoint::a, Endpoint::b, one_byte(1U),
                              LogicalTick{0U}).ok());
        RL_REQUIRE(probe.send(Endpoint::a, Endpoint::b, one_byte(2U),
                              LogicalTick{0U}).ok());
        std::vector<Delivery> delivered;
        for (std::uint32_t tick = 0U; tick <= 40U; ++tick) {
            auto batch = probe.deliver(LogicalTick{tick});
            if (delivered.empty() && !batch.empty()) {
                first_delivery_tick = tick;
            }
            delivered.insert(delivered.end(), batch.begin(), batch.end());
        }
        if (delivered.size() == 2U &&
            delivered.front().bytes == one_byte(2U)) {
            inversion_seed = seed;
            break;
        }
    }
    RL_REQUIRE(inversion_seed != 0U);
    RL_REQUIRE(first_delivery_tick > 2U);

    TransportConfig config{};
    config.seed = inversion_seed;
    config.base_latency_ticks = 10U;
    config.jitter_ticks = 4U;
    config.reorder_percent = 100U;
    config.max_queue_packets = 2U;
    config.max_queue_bytes = 2U;
    config.overflow_policy = QueueOverflowPolicy::drop_oldest;
    SeededTransport transport{config};

    RL_REQUIRE(transport.send(Endpoint::a, Endpoint::b, one_byte(1U),
                              LogicalTick{0U}).ok());
    RL_REQUIRE(transport.send(Endpoint::a, Endpoint::b, one_byte(2U),
                              LogicalTick{0U}).ok());
    RL_CHECK(transport.deliver(LogicalTick{first_delivery_tick - 1U}).empty());
    RL_REQUIRE(transport.send(Endpoint::a, Endpoint::b, one_byte(3U),
                              LogicalTick{first_delivery_tick - 1U}).ok());

    const auto first_due = transport.deliver(LogicalTick{first_delivery_tick});
    RL_REQUIRE(first_due.size() == 1U);
    RL_CHECK(first_due.front().bytes == one_byte(2U));
    RL_CHECK(transport.metrics().dropped_overflow == 1U);
}

RL_TEST(transport_bandwidth_defers_due_packets_without_unbounded_growth) {
    TransportConfig config{};
    config.seed = 3U;
    config.bandwidth_bytes_per_tick = 2U;
    SeededTransport transport{config};
    for (std::uint8_t index = 0U; index < 5U; ++index) {
        RL_REQUIRE(transport.send(Endpoint::a, Endpoint::b, one_byte(index),
                                  LogicalTick{0U}).ok());
    }
    RL_CHECK(transport.deliver(LogicalTick{0U}).size() == 2U);
    RL_CHECK(transport.deliver(LogicalTick{1U}).size() == 2U);
    RL_CHECK(transport.deliver(LogicalTick{2U}).size() == 1U);
    RL_CHECK(transport.queued_packets() == 0U);
}

RL_TEST(random_protocol_bytes_never_escape_typed_decode_result) {
    Pcg32 random{0xBAD5EEDU};
    for (std::uint32_t iteration = 0; iteration < 10'000U; ++iteration) {
        const auto size = static_cast<std::size_t>(random.bounded(256U));
        std::vector<std::byte> bytes(size);
        for (auto& byte : bytes) {
            byte = static_cast<std::byte>(random.next_u32() & 0xFFU);
        }
        const auto decoded = decode_packet(bytes);
        if (decoded.ok()) {
            const auto canonical = encode_packet(decoded.value());
            RL_REQUIRE(canonical.ok());
            RL_REQUIRE(decode_packet(canonical.value()).ok());
        } else {
            RL_CHECK(decoded.error().code != ErrorCode::none);
        }
    }
}
