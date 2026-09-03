#include <rollback_lab/core/pcg32.hpp>
#include <rollback_lab/protocol/codec.hpp>
#include <rollback_lab/protocol/crc32.hpp>
#include <rollback_lab/replay/replay.hpp>
#include <rollback_lab/simulation/scripted_input.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <vector>

namespace {

using namespace rollback_lab;

void rewrite_crc(std::vector<std::byte>& bytes) {
    if (bytes.size() < 4U) {
        return;
    }
    const auto checksum = crc32(
        std::span<const std::byte>{bytes}.first(bytes.size() - 4U));
    for (std::size_t index = 0U; index < 4U; ++index) {
        bytes[bytes.size() - 4U + index] = static_cast<std::byte>(
            (checksum >> static_cast<unsigned>(index * 8U)) & 0xFFU);
    }
}

auto packet_corpus() -> std::array<std::vector<std::byte>, 4U> {
    std::array<Packet, 4U> packets{};
    packets[0].type = PacketType::hello;
    packets[0].hello = HelloInfo{kSimulationVersion,
                                 0xAABBCCDDEEFF0011ULL, 240U};

    packets[1].type = PacketType::input;
    packets[1].sender = PlayerId::a;
    packets[1].sequence = 10U;
    packets[1].scenario_id = 42U;
    for (std::uint32_t frame = 0U; frame < 8U; ++frame) {
        packets[1].inputs.push_back(scripted_input(42U, FrameNumber{frame},
                                                   PlayerId::a));
        packets[1].confirmed_hashes.push_back(
            ConfirmedHash{FrameNumber{frame}, frame * 0x102030405ULL});
    }

    packets[2].type = PacketType::state_hash;
    packets[2].sender = PlayerId::b;
    packets[2].sequence = 11U;
    packets[2].confirmed_hashes = {
        ConfirmedHash{FrameNumber{20U}, 1U},
        ConfirmedHash{FrameNumber{21U}, 2U},
        ConfirmedHash{FrameNumber{22U}, 3U}};

    packets[3].type = PacketType::goodbye;
    packets[3].sender = PlayerId::b;
    packets[3].sequence = 12U;

    std::array<std::vector<std::byte>, 4U> encoded{};
    for (std::size_t index = 0U; index < packets.size(); ++index) {
        const auto result = encode_packet(packets[index]);
        if (!result.ok()) {
            return {};
        }
        encoded[index] = result.value();
    }
    return encoded;
}

auto replay_corpus() -> std::vector<std::byte> {
    Replay replay{};
    replay.scenario_seed = 77U;
    replay.transport_seed = 88U;
    replay.final_frame = FrameNumber{16U};
    auto state = make_initial_world();
    for (std::uint32_t frame = 0U; frame < replay.final_frame.value; ++frame) {
        const auto number = FrameNumber{frame};
        const InputPair inputs{scripted_input(77U, number, PlayerId::a),
                               scripted_input(77U, number, PlayerId::b)};
        replay.confirmed_inputs.push_back(inputs);
        const auto next = simulate_frame(state, number, inputs);
        if (!next.ok()) {
            return {};
        }
        state = next.value();
        if (state.frame.value % 4U == 0U) {
            replay.checkpoints.push_back(
                ReplayCheckpoint{state.frame, hash_canonical(state)});
        }
    }
    replay.expected_final_hash = hash_canonical(state);
    const auto encoded = encode_replay(replay);
    return encoded.ok() ? encoded.value() : std::vector<std::byte>{};
}

void mutate(std::vector<std::byte>& bytes, Pcg32& random) {
    if (bytes.empty()) {
        return;
    }
    const auto mutations = 1U + random.bounded(4U);
    for (std::uint32_t mutation = 0U; mutation < mutations; ++mutation) {
        const auto index = static_cast<std::size_t>(
            random.bounded(static_cast<std::uint32_t>(bytes.size())));
        bytes[index] ^= static_cast<std::byte>(
            1U << static_cast<std::uint8_t>(random.bounded(8U)));
    }
}

}  // namespace

int main() {
    Pcg32 random{0xF0225EEDU};
    const auto packets = packet_corpus();
    const auto replay = replay_corpus();
    if (packets[0].empty() || replay.empty()) {
        std::cerr << "failed to build fuzz corpus\n";
        return 1;
    }

    constexpr std::uint32_t iterations = 100'000U;
    std::uint32_t random_packets{};
    std::uint32_t structured_packets{};
    std::uint32_t structured_replays{};
    std::uint32_t crc_rewrites{};
    for (std::uint32_t iteration = 0U; iteration < iterations; ++iteration) {
        if (iteration % 3U == 0U) {
            ++random_packets;
            const auto size = static_cast<std::size_t>(random.bounded(1'201U));
            std::vector<std::byte> bytes(size);
            for (auto& byte : bytes) {
                byte = static_cast<std::byte>(random.next_u32() & 0xFFU);
            }
            const auto decoded = decode_packet(bytes);
            if (decoded.ok() && !encode_packet(decoded.value()).ok()) {
                return 2;
            }
            continue;
        }

        if (iteration % 3U == 1U) {
            ++structured_packets;
            auto bytes = packets[iteration % packets.size()];
            mutate(bytes, random);
            if (iteration % 4U != 0U) {
                rewrite_crc(bytes);
                ++crc_rewrites;
            }
            const auto decoded = decode_packet(bytes);
            if (decoded.ok()) {
                const auto canonical = encode_packet(decoded.value());
                if (!canonical.ok() ||
                    !decode_packet(canonical.value()).ok()) {
                    return 3;
                }
            }
            continue;
        }

        ++structured_replays;
        auto bytes = replay;
        mutate(bytes, random);
        if (iteration % 4U != 0U) {
            rewrite_crc(bytes);
            ++crc_rewrites;
        }
        const auto decoded = decode_replay(bytes);
        if (decoded.ok()) {
            const auto canonical = encode_replay(decoded.value());
            if (!canonical.ok() || !decode_replay(canonical.value()).ok()) {
                return 4;
            }
            static_cast<void>(verify_replay(decoded.value()));
        }
    }
    std::cout << "protocol/replay fuzz smoke: " << iterations
              << " inputs (random_packet=" << random_packets
              << ", structured_packet=" << structured_packets
              << ", structured_replay=" << structured_replays
              << ", crc_rewrites=" << crc_rewrites << "), 0 crashes\n";
    return 0;
}

