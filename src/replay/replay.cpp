#include <rollback_lab/replay/replay.hpp>

#include <rollback_lab/protocol/bytes.hpp>
#include <rollback_lab/protocol/crc32.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

namespace rollback_lab {
namespace {

constexpr std::size_t header_size = 48U;
constexpr std::size_t input_pair_size = 14U;
constexpr std::size_t checkpoint_size = 12U;
constexpr std::size_t checksum_size = 4U;

auto failure(const ErrorCode code,
             const std::size_t offset,
             const char* context,
             const std::uint64_t detail = 0U) -> Result<Replay> {
    return Result<Replay>::failure(Error{code, detail, offset, context});
}

auto stored_crc(const std::span<const std::byte> bytes) -> std::uint32_t {
    const auto offset = bytes.size() - checksum_size;
    std::uint32_t value{};
    for (std::size_t index = 0U; index < checksum_size; ++index) {
        value |= static_cast<std::uint32_t>(
                     std::to_integer<std::uint8_t>(bytes[offset + index]))
                 << static_cast<unsigned>(index * 8U);
    }
    return value;
}

auto valid_variant(const SimulationVariant variant) noexcept -> bool {
    return variant == SimulationVariant::canonical ||
           variant == SimulationVariant::damage_bias;
}

}  // namespace

auto encode_replay(const Replay& replay) -> Result<std::vector<std::byte>> {
    if (replay.replay_version != kReplayVersion ||
        replay.simulation_version != kSimulationVersion ||
        replay.protocol_version != kProtocolVersion) {
        return Result<std::vector<std::byte>>::failure(
            Error{ErrorCode::unsupported_version, replay.replay_version, 4U,
                  "replay_versions"});
    }
    if (!valid_variant(replay.variant)) {
        return Result<std::vector<std::byte>>::failure(
            Error{ErrorCode::invalid_argument,
                  static_cast<std::uint64_t>(replay.variant), 10U,
                  "simulation_variant"});
    }
    if (replay.confirmed_inputs.size() > kMaxReplayFrames ||
        replay.checkpoints.size() > kMaxReplayCheckpoints ||
        replay.confirmed_inputs.size() != replay.final_frame.value) {
        return Result<std::vector<std::byte>>::failure(
            Error{ErrorCode::capacity_exceeded, replay.confirmed_inputs.size(),
                  32U, "replay_counts"});
    }

    ByteWriter writer;
    writer.append_little_endian(kReplayMagic);
    writer.append_little_endian(replay.replay_version);
    writer.append_little_endian(replay.simulation_version);
    writer.append_little_endian(replay.protocol_version);
    writer.append_little_endian(static_cast<std::uint8_t>(replay.variant));
    writer.append_little_endian(std::uint8_t{0U});
    writer.append_little_endian(replay.scenario_seed);
    writer.append_little_endian(replay.transport_seed);
    writer.append_little_endian(replay.final_frame.value);
    writer.append_little_endian(
        static_cast<std::uint32_t>(replay.confirmed_inputs.size()));
    writer.append_little_endian(
        static_cast<std::uint32_t>(replay.checkpoints.size()));
    writer.append_little_endian(replay.expected_final_hash);

    for (const auto& inputs : replay.confirmed_inputs) {
        if (inputs.a.player != PlayerId::a || inputs.b.player != PlayerId::b ||
            inputs.a.frame != inputs.b.frame) {
            return Result<std::vector<std::byte>>::failure(
                Error{ErrorCode::invalid_argument, inputs.a.frame.value, 0U,
                      "replay_input_pair"});
        }
        writer.append_little_endian(inputs.a.frame.value);
        writer.append_little_endian(inputs.a.buttons);
        writer.append_little_endian(inputs.a.sequence);
        writer.append_little_endian(inputs.b.buttons);
        writer.append_little_endian(inputs.b.sequence);
    }
    for (const auto& checkpoint : replay.checkpoints) {
        writer.append_little_endian(checkpoint.frame.value);
        writer.append_little_endian(checkpoint.hash);
    }
    writer.append_little_endian(crc32(writer.bytes()));
    return Result<std::vector<std::byte>>::success(writer.take());
}

auto decode_replay(const std::span<const std::byte> bytes) -> Result<Replay> {
    if (bytes.size() < header_size + checksum_size) {
        return failure(ErrorCode::truncated_data, bytes.size(), "replay");
    }
    if (stored_crc(bytes) != crc32(bytes.first(bytes.size() - checksum_size))) {
        return failure(ErrorCode::integrity_mismatch,
                       bytes.size() - checksum_size, "replay_crc");
    }

    ByteReader reader{bytes.first(bytes.size() - checksum_size)};
    const auto magic = reader.read_little_endian<std::uint32_t>("magic");
    const auto replay_version =
        reader.read_little_endian<std::uint16_t>("replay_version");
    const auto simulation_version =
        reader.read_little_endian<std::uint16_t>("simulation_version");
    const auto protocol_version =
        reader.read_little_endian<std::uint16_t>("protocol_version");
    const auto variant = reader.read_little_endian<std::uint8_t>("variant");
    const auto reserved = reader.read_little_endian<std::uint8_t>("reserved");
    const auto scenario = reader.read_little_endian<std::uint64_t>("scenario");
    const auto transport = reader.read_little_endian<std::uint64_t>("transport");
    const auto final_frame =
        reader.read_little_endian<std::uint32_t>("final_frame");
    const auto input_count =
        reader.read_little_endian<std::uint32_t>("input_count");
    const auto checkpoint_count =
        reader.read_little_endian<std::uint32_t>("checkpoint_count");
    const auto expected_hash =
        reader.read_little_endian<std::uint64_t>("expected_hash");
    if (!magic.ok() || !replay_version.ok() || !simulation_version.ok() ||
        !protocol_version.ok() || !variant.ok() || !reserved.ok() ||
        !scenario.ok() || !transport.ok() || !final_frame.ok() ||
        !input_count.ok() || !checkpoint_count.ok() || !expected_hash.ok()) {
        return failure(ErrorCode::truncated_data, reader.offset(),
                       "replay_header");
    }
    if (magic.value() != kReplayMagic) {
        return failure(ErrorCode::invalid_magic, 0U, "replay_magic",
                       magic.value());
    }
    if (replay_version.value() != kReplayVersion ||
        simulation_version.value() != kSimulationVersion ||
        protocol_version.value() != kProtocolVersion) {
        return failure(ErrorCode::unsupported_version, 4U, "replay_versions",
                       replay_version.value());
    }
    const auto decoded_variant = static_cast<SimulationVariant>(variant.value());
    if (!valid_variant(decoded_variant) || reserved.value() != 0U) {
        return failure(ErrorCode::invalid_argument, 10U, "replay_flags",
                       variant.value());
    }
    if (input_count.value() > kMaxReplayFrames ||
        checkpoint_count.value() > kMaxReplayCheckpoints ||
        input_count.value() != final_frame.value()) {
        return failure(ErrorCode::capacity_exceeded, 32U, "replay_counts",
                       input_count.value());
    }
    const auto expected_size = header_size +
        static_cast<std::size_t>(input_count.value()) * input_pair_size +
        static_cast<std::size_t>(checkpoint_count.value()) * checkpoint_size +
        checksum_size;
    if (bytes.size() != expected_size) {
        return failure(ErrorCode::invalid_length, reader.offset(),
                       "replay_size", expected_size);
    }

    Replay replay{};
    replay.replay_version = replay_version.value();
    replay.simulation_version = simulation_version.value();
    replay.protocol_version = protocol_version.value();
    replay.variant = decoded_variant;
    replay.scenario_seed = scenario.value();
    replay.transport_seed = transport.value();
    replay.final_frame = FrameNumber{final_frame.value()};
    replay.expected_final_hash = expected_hash.value();
    replay.confirmed_inputs.reserve(input_count.value());
    for (std::uint32_t index = 0U; index < input_count.value(); ++index) {
        const auto frame = reader.read_little_endian<std::uint32_t>("frame");
        const auto buttons_a = reader.read_little_endian<std::uint8_t>("buttons_a");
        const auto sequence_a =
            reader.read_little_endian<std::uint32_t>("sequence_a");
        const auto buttons_b = reader.read_little_endian<std::uint8_t>("buttons_b");
        const auto sequence_b =
            reader.read_little_endian<std::uint32_t>("sequence_b");
        if (!frame.ok() || !buttons_a.ok() || !sequence_a.ok() ||
            !buttons_b.ok() || !sequence_b.ok()) {
            return failure(ErrorCode::truncated_data, reader.offset(),
                           "replay_input");
        }
        if (frame.value() != index || (buttons_a.value() & 0xE0U) != 0U ||
            (buttons_b.value() & 0xE0U) != 0U) {
            return failure(ErrorCode::invalid_argument, reader.offset(),
                           "replay_input", frame.value());
        }
        const auto number = FrameNumber{frame.value()};
        replay.confirmed_inputs.push_back(InputPair{
            InputFrame{number, PlayerId::a, sequence_a.value(), buttons_a.value()},
            InputFrame{number, PlayerId::b, sequence_b.value(), buttons_b.value()}});
    }
    replay.checkpoints.reserve(checkpoint_count.value());
    FrameNumber previous{};
    bool first = true;
    for (std::uint32_t index = 0U; index < checkpoint_count.value(); ++index) {
        const auto frame =
            reader.read_little_endian<std::uint32_t>("checkpoint_frame");
        const auto hash =
            reader.read_little_endian<std::uint64_t>("checkpoint_hash");
        if (!frame.ok() || !hash.ok()) {
            return failure(ErrorCode::truncated_data, reader.offset(),
                           "replay_checkpoint");
        }
        const auto number = FrameNumber{frame.value()};
        if (frame.value() > final_frame.value() ||
            (!first && !frame_before(previous, number))) {
            return failure(ErrorCode::invalid_argument, reader.offset(),
                           "checkpoint_order", frame.value());
        }
        first = false;
        previous = number;
        replay.checkpoints.push_back(ReplayCheckpoint{number, hash.value()});
    }
    return Result<Replay>::success(std::move(replay));
}

auto verify_replay(const Replay& replay) -> Result<ReplayVerification> {
    if (replay.replay_version != kReplayVersion ||
        replay.simulation_version != kSimulationVersion ||
        replay.protocol_version != kProtocolVersion ||
        replay.confirmed_inputs.size() != replay.final_frame.value) {
        return Result<ReplayVerification>::failure(
            Error{ErrorCode::unsupported_version, replay.replay_version, 0U,
                  "verify_replay"});
    }
    auto state = make_initial_world();
    std::size_t checkpoint_index = 0U;
    for (std::uint32_t frame = 0U; frame < replay.final_frame.value; ++frame) {
        const auto& inputs = replay.confirmed_inputs[frame];
        if (inputs.a.frame.value != frame || inputs.b.frame.value != frame) {
            return Result<ReplayVerification>::failure(
                Error{ErrorCode::frame_mismatch, frame, 0U, "replay_frame"});
        }
        const auto next = simulate_frame(state, FrameNumber{frame}, inputs,
                                         replay.variant);
        if (!next.ok()) {
            return Result<ReplayVerification>::failure(next.error());
        }
        state = next.value();
        if (checkpoint_index < replay.checkpoints.size() &&
            replay.checkpoints[checkpoint_index].frame == state.frame) {
            const auto actual = hash_canonical(state);
            if (actual != replay.checkpoints[checkpoint_index].hash) {
                return Result<ReplayVerification>::failure(
                    Error{ErrorCode::replay_mismatch, actual, state.frame.value,
                          "checkpoint_hash"});
            }
            ++checkpoint_index;
        }
    }
    const auto final_hash = hash_canonical(state);
    if (checkpoint_index != replay.checkpoints.size() ||
        final_hash != replay.expected_final_hash) {
        return Result<ReplayVerification>::failure(
            Error{ErrorCode::replay_mismatch, final_hash,
                  replay.final_frame.value, "final_hash"});
    }
    return Result<ReplayVerification>::success(
        ReplayVerification{true, replay.final_frame, final_hash});
}

}  // namespace rollback_lab

