#include <rollback_lab/protocol/codec.hpp>

#include <rollback_lab/protocol/bytes.hpp>
#include <rollback_lab/protocol/crc32.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>

namespace rollback_lab {
namespace {

constexpr std::size_t header_size = 32U;
constexpr std::size_t checksum_size = 4U;
constexpr std::size_t input_record_size = 10U;
constexpr std::size_t hash_record_size = 12U;
constexpr std::uint8_t hash_flag = 0x01U;

auto valid_packet_type(const PacketType type) noexcept -> bool {
    return type == PacketType::hello || type == PacketType::input ||
           type == PacketType::state_hash || type == PacketType::goodbye;
}

auto read_checksum(const std::span<const std::byte> bytes) -> std::uint32_t {
    const auto offset = bytes.size() - checksum_size;
    std::uint32_t value{};
    for (std::size_t index = 0; index < checksum_size; ++index) {
        value |= static_cast<std::uint32_t>(
                     std::to_integer<std::uint8_t>(bytes[offset + index]))
                 << static_cast<unsigned>(index * 8U);
    }
    return value;
}

auto decode_error(const ErrorCode code,
                  const std::size_t offset,
                  const char* context,
                  const std::uint64_t detail = 0U) -> Result<Packet> {
    return Result<Packet>::failure(Error{code, detail, offset, context});
}

}  // namespace

auto encode_packet(const Packet& packet) -> Result<std::vector<std::byte>> {
    if (packet.protocol_version != kProtocolVersion) {
        return Result<std::vector<std::byte>>::failure(
            Error{ErrorCode::unsupported_version, packet.protocol_version, 4U,
                  "protocol_version"});
    }
    if (!valid_packet_type(packet.type)) {
        return Result<std::vector<std::byte>>::failure(
            Error{ErrorCode::unknown_packet_type,
                  static_cast<std::uint64_t>(packet.type), 6U, "packet_type"});
    }
    if (packet.inputs.size() > kMaxRedundantInputs) {
        return Result<std::vector<std::byte>>::failure(
            Error{ErrorCode::capacity_exceeded, packet.inputs.size(), 28U,
                  "input_count"});
    }
    for (const auto& input : packet.inputs) {
        if (input.player != packet.sender) {
            return Result<std::vector<std::byte>>::failure(
                Error{ErrorCode::invalid_argument,
                      static_cast<std::uint64_t>(input.player), 0U,
                      "input_sender"});
        }
    }

    ByteWriter writer;
    writer.append_little_endian(kPacketMagic);
    writer.append_little_endian(packet.protocol_version);
    writer.append_little_endian(static_cast<std::uint8_t>(packet.type));
    writer.append_little_endian(static_cast<std::uint8_t>(packet.sender));
    writer.append_little_endian(packet.sequence);
    writer.append_little_endian(packet.ack);
    writer.append_little_endian(packet.scenario_id);
    writer.append_little_endian(packet.confirmed_frame.value);
    writer.append_little_endian(static_cast<std::uint8_t>(packet.inputs.size()));
    writer.append_little_endian(
        static_cast<std::uint8_t>(packet.confirmed_hash.has_value() ? hash_flag
                                                                   : 0U));
    constexpr std::size_t payload_length_offset = 30U;
    writer.append_little_endian(std::uint16_t{0U});
    const auto payload_start = writer.size();

    for (const auto& input : packet.inputs) {
        writer.append_little_endian(input.frame.value);
        writer.append_little_endian(static_cast<std::uint8_t>(input.player));
        writer.append_little_endian(input.buttons);
        writer.append_little_endian(input.sequence);
    }
    if (packet.confirmed_hash.has_value()) {
        writer.append_little_endian(packet.confirmed_hash->frame.value);
        writer.append_little_endian(packet.confirmed_hash->hash);
    }

    const auto payload_size = writer.size() - payload_start;
    if (payload_size > std::numeric_limits<std::uint16_t>::max()) {
        return Result<std::vector<std::byte>>::failure(
            Error{ErrorCode::invalid_length, payload_size, payload_length_offset,
                  "payload_length"});
    }
    writer.patch_u16(payload_length_offset,
                     static_cast<std::uint16_t>(payload_size));
    const auto checksum = crc32(writer.bytes());
    writer.append_little_endian(checksum);
    if (writer.size() > kMaxPacketBytes) {
        return Result<std::vector<std::byte>>::failure(
            Error{ErrorCode::invalid_length, writer.size(), 0U, "packet_size"});
    }
    return Result<std::vector<std::byte>>::success(writer.take());
}

auto decode_packet(const std::span<const std::byte> bytes) -> Result<Packet> {
    if (bytes.size() < header_size + checksum_size) {
        return decode_error(ErrorCode::truncated_data, bytes.size(), "packet");
    }
    if (bytes.size() > kMaxPacketBytes) {
        return decode_error(ErrorCode::invalid_length, 0U, "packet_size",
                            bytes.size());
    }
    const auto stored_checksum = read_checksum(bytes);
    const auto computed_checksum = crc32(bytes.first(bytes.size() - checksum_size));
    if (stored_checksum != computed_checksum) {
        return decode_error(ErrorCode::integrity_mismatch,
                            bytes.size() - checksum_size, "crc32",
                            stored_checksum);
    }

    ByteReader reader{bytes.first(bytes.size() - checksum_size)};
    const auto magic = reader.read_little_endian<std::uint32_t>("magic");
    if (!magic.ok() || magic.value() != kPacketMagic) {
        return decode_error(ErrorCode::invalid_magic, 0U, "magic",
                            magic.ok() ? magic.value() : 0U);
    }
    const auto version = reader.read_little_endian<std::uint16_t>("version");
    if (!version.ok() || version.value() != kProtocolVersion) {
        return decode_error(ErrorCode::unsupported_version, 4U, "version",
                            version.ok() ? version.value() : 0U);
    }
    const auto type_byte = reader.read_little_endian<std::uint8_t>("type");
    if (!type_byte.ok()) {
        return decode_error(type_byte.error().code, type_byte.error().offset,
                            type_byte.error().context);
    }
    const auto type = static_cast<PacketType>(type_byte.value());
    if (!valid_packet_type(type)) {
        return decode_error(ErrorCode::unknown_packet_type, 6U, "type",
                            type_byte.value());
    }
    const auto sender_byte = reader.read_little_endian<std::uint8_t>("sender");
    if (!sender_byte.ok() || sender_byte.value() > 1U) {
        return decode_error(ErrorCode::invalid_argument, 7U, "sender",
                            sender_byte.ok() ? sender_byte.value() : 0U);
    }
    const auto sequence = reader.read_little_endian<std::uint32_t>("sequence");
    const auto ack = reader.read_little_endian<std::uint32_t>("ack");
    const auto scenario = reader.read_little_endian<std::uint64_t>("scenario");
    const auto confirmed =
        reader.read_little_endian<std::uint32_t>("confirmed_frame");
    const auto count = reader.read_little_endian<std::uint8_t>("input_count");
    const auto flags = reader.read_little_endian<std::uint8_t>("flags");
    const auto payload_length =
        reader.read_little_endian<std::uint16_t>("payload_length");
    if (!sequence.ok() || !ack.ok() || !scenario.ok() || !confirmed.ok() ||
        !count.ok() || !flags.ok() || !payload_length.ok()) {
        return decode_error(ErrorCode::truncated_data, reader.offset(), "header");
    }
    if (count.value() > kMaxRedundantInputs) {
        return decode_error(ErrorCode::capacity_exceeded, 28U, "input_count",
                            count.value());
    }
    if ((flags.value() & static_cast<std::uint8_t>(~hash_flag)) != 0U) {
        return decode_error(ErrorCode::invalid_argument, 29U, "flags",
                            flags.value());
    }
    const auto expected_payload =
        static_cast<std::size_t>(count.value()) * input_record_size +
        ((flags.value() & hash_flag) != 0U ? hash_record_size : 0U);
    if (payload_length.value() != expected_payload ||
        header_size + expected_payload + checksum_size != bytes.size()) {
        return decode_error(ErrorCode::invalid_length, 30U, "payload_length",
                            payload_length.value());
    }

    Packet packet{};
    packet.protocol_version = version.value();
    packet.type = type;
    packet.sender = static_cast<PlayerId>(sender_byte.value());
    packet.sequence = sequence.value();
    packet.ack = ack.value();
    packet.scenario_id = scenario.value();
    packet.confirmed_frame = FrameNumber{confirmed.value()};
    packet.inputs.reserve(count.value());
    for (std::uint8_t index = 0U; index < count.value(); ++index) {
        const auto frame = reader.read_little_endian<std::uint32_t>("input_frame");
        const auto player = reader.read_little_endian<std::uint8_t>("input_player");
        const auto buttons = reader.read_little_endian<std::uint8_t>("buttons");
        const auto input_sequence =
            reader.read_little_endian<std::uint32_t>("input_sequence");
        if (!frame.ok() || !player.ok() || !buttons.ok() ||
            !input_sequence.ok()) {
            return decode_error(ErrorCode::truncated_data, reader.offset(),
                                "input_record");
        }
        if (player.value() != sender_byte.value() ||
            (buttons.value() & 0xE0U) != 0U) {
            return decode_error(ErrorCode::invalid_argument,
                                reader.offset() - input_record_size,
                                "input_record");
        }
        packet.inputs.push_back(InputFrame{
            FrameNumber{frame.value()}, static_cast<PlayerId>(player.value()),
            input_sequence.value(), buttons.value()});
    }
    if ((flags.value() & hash_flag) != 0U) {
        const auto hash_frame =
            reader.read_little_endian<std::uint32_t>("hash_frame");
        const auto hash = reader.read_little_endian<std::uint64_t>("hash");
        if (!hash_frame.ok() || !hash.ok()) {
            return decode_error(ErrorCode::truncated_data, reader.offset(),
                                "confirmed_hash");
        }
        packet.confirmed_hash =
            ConfirmedHash{FrameNumber{hash_frame.value()}, hash.value()};
    }
    if (reader.remaining() != 0U) {
        return decode_error(ErrorCode::invalid_length, reader.offset(),
                            "trailing_payload", reader.remaining());
    }
    return Result<Packet>::success(std::move(packet));
}

}  // namespace rollback_lab

