#include <rollback_lab/report/canonical_json.hpp>

#include <rollback_lab/core/hash.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace rollback_lab {
namespace {

auto escaped(const std::string_view value) -> std::string {
    std::ostringstream output;
    for (const unsigned char character : value) {
        switch (character) {
        case '"': output << "\\\""; break;
        case '\\': output << "\\\\"; break;
        case '\b': output << "\\b"; break;
        case '\f': output << "\\f"; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default:
            if (character < 0x20U) {
                output << "\\u" << std::hex << std::setw(4)
                       << std::setfill('0') << static_cast<unsigned>(character)
                       << std::dec;
            } else {
                output << static_cast<char>(character);
            }
            break;
        }
    }
    return output.str();
}

auto hash_text(const StateHash hash) -> std::string {
    std::ostringstream output;
    output << "0x" << std::hex << std::uppercase << std::setw(16)
           << std::setfill('0') << hash;
    return output.str();
}

auto endpoint_text(const Endpoint endpoint) -> const char* {
    switch (endpoint) {
    case Endpoint::a: return "A";
    case Endpoint::b: return "B";
    case Endpoint::relay: return "relay";
    }
    return "unknown";
}

auto packet_kind_text(const PacketTraceKind kind) -> const char* {
    switch (kind) {
    case PacketTraceKind::sent: return "sent";
    case PacketTraceKind::delivered: return "delivered";
    case PacketTraceKind::dropped: return "dropped";
    case PacketTraceKind::duplicated: return "duplicated";
    case PacketTraceKind::reordered: return "reordered";
    }
    return "unknown";
}

auto overflow_policy_text(const QueueOverflowPolicy policy) -> const char* {
    return policy == QueueOverflowPolicy::fail ? "fail" : "drop_oldest";
}

void write_player(std::ostringstream& output, const PlayerState& player) {
    output << "{\"id\":\"" << (player.id == PlayerId::a ? "A" : "B")
           << "\",\"x\":" << player.x
           << ",\"y\":" << player.y
           << ",\"velocity_x\":" << player.velocity_x
           << ",\"velocity_y\":" << player.velocity_y
           << ",\"facing_x\":" << static_cast<int>(player.facing_x)
           << ",\"facing_y\":" << static_cast<int>(player.facing_y)
           << ",\"hp\":" << player.hp
           << ",\"score\":" << player.score
           << ",\"attack_cooldown\":" << player.attack_cooldown << '}';
}

auto identity_material(const RunReport& report) -> std::string {
    std::ostringstream output;
    output << report.simulation_version << '|'
           << report.protocol_version << '|'
           << report.pcg32_version << '|'
           << report.scenario_seed << '|'
           << report.transport_seed << '|'
           << report.frame_count << '|'
           << report.transport_config.base_latency_ticks << '|'
           << report.transport_config.jitter_ticks << '|'
           << report.transport_config.loss_percent << '|'
           << report.transport_config.reorder_percent << '|'
           << report.transport_config.duplicate_percent << '|'
           << report.transport_config.burst_loss_percent << '|'
           << report.transport_config.max_queue_packets << '|'
           << report.transport_config.max_queue_bytes << '|'
           << report.transport_config.bandwidth_bytes_per_tick << '|'
           << report.transport_config.max_packet_age_ticks << '|'
           << static_cast<std::uint32_t>(report.transport_config.overflow_policy)
           << '|'
           << report.transport_metrics.sent_packets << '|'
           << report.transport_metrics.delivered_packets << '|'
           << report.transport_metrics.dropped_loss << '|'
           << report.transport_metrics.dropped_overflow << '|'
           << report.transport_metrics.dropped_age << '|'
           << report.transport_metrics.duplicated_packets << '|'
           << report.transport_metrics.reordered_packets << '|'
           << report.rollback_count << '|'
           << report.resimulated_frames << '|'
           << report.maximum_rollback_depth << '|'
           << report.predicted_input_count << '|'
           << report.late_input_count << '|'
           << report.confirmed_frame.value << '|'
           << report.final_hash_a << '|'
           << report.final_hash_b << '|'
           << report.replay_verified << '|'
           << report.desync_result << '|'
           << report.success << '|'
           << report.failure_reason;
    return output.str();
}

auto hash_string(const std::string& value) -> StateHash {
    std::vector<std::byte> bytes;
    bytes.reserve(value.size());
    for (const unsigned char character : value) {
        bytes.push_back(static_cast<std::byte>(character));
    }
    return fnv1a64(bytes);
}

}  // namespace

auto report_identity(const RunReport& report) -> StateHash {
    return hash_string(identity_material(report));
}

auto canonical_json(const RunReport& report) -> Result<std::string> {
    std::ostringstream output;
    output << "{\n"
           << "  \"git_sha\":\"" << escaped(report.git_sha) << "\",\n"
           << "  \"build_type\":\"" << escaped(report.build_type) << "\",\n"
           << "  \"compiler\":\"" << escaped(report.compiler) << "\",\n"
           << "  \"os\":\"" << escaped(report.os) << "\",\n"
           << "  \"simulation_version\":" << report.simulation_version << ",\n"
           << "  \"protocol_version\":" << report.protocol_version << ",\n"
           << "  \"pcg32_version\":" << report.pcg32_version << ",\n"
           << "  \"scenario_seed\":" << report.scenario_seed << ",\n"
           << "  \"transport_seed\":" << report.transport_seed << ",\n"
           << "  \"frame_count\":" << report.frame_count << ",\n"
           << "  \"transport_config\":{\"base_latency_ticks\":"
           << report.transport_config.base_latency_ticks
           << ",\"jitter_ticks\":" << report.transport_config.jitter_ticks
           << ",\"loss_percent\":" << report.transport_config.loss_percent
           << ",\"reorder_percent\":" << report.transport_config.reorder_percent
           << ",\"duplicate_percent\":"
           << report.transport_config.duplicate_percent
           << ",\"burst_loss_percent\":"
           << report.transport_config.burst_loss_percent
           << ",\"max_queue_packets\":"
           << report.transport_config.max_queue_packets
           << ",\"max_queue_bytes\":" << report.transport_config.max_queue_bytes
           << ",\"bandwidth_bytes_per_tick\":"
           << report.transport_config.bandwidth_bytes_per_tick
           << ",\"max_packet_age_ticks\":"
           << report.transport_config.max_packet_age_ticks
           << ",\"overflow_policy\":\""
           << overflow_policy_text(report.transport_config.overflow_policy)
           << "\"},\n"
           << "  \"sent_packets\":" << report.transport_metrics.sent_packets << ",\n"
           << "  \"delivered_packets\":"
           << report.transport_metrics.delivered_packets << ",\n"
           << "  \"dropped_packets\":"
           << (report.transport_metrics.dropped_loss +
               report.transport_metrics.dropped_overflow +
               report.transport_metrics.dropped_age)
           << ",\n"
           << "  \"duplicated_packets\":"
           << report.transport_metrics.duplicated_packets << ",\n"
           << "  \"reordered_packets\":"
           << report.transport_metrics.reordered_packets << ",\n"
           << "  \"rollback_count\":" << report.rollback_count << ",\n"
           << "  \"resimulated_frames\":" << report.resimulated_frames << ",\n"
           << "  \"maximum_rollback_depth\":"
           << report.maximum_rollback_depth << ",\n"
           << "  \"predicted_input_count\":"
           << report.predicted_input_count << ",\n"
           << "  \"late_input_count\":" << report.late_input_count << ",\n"
           << "  \"confirmed_frame\":" << report.confirmed_frame.value << ",\n"
           << "  \"final_hash_a\":\"" << hash_text(report.final_hash_a)
           << "\",\n"
           << "  \"final_hash_b\":\"" << hash_text(report.final_hash_b)
           << "\",\n"
           << "  \"replay_verification\":"
           << (report.replay_verified ? "true" : "false") << ",\n"
           << "  \"desync_result\":\"" << escaped(report.desync_result)
           << "\",\n"
           << "  \"timing_observations\":{\"simulation_microseconds\":"
           << report.timing.simulation_microseconds
           << ",\"rollback_microseconds\":"
           << report.timing.rollback_microseconds << "},\n"
           << "  \"identity_digest\":\"" << hash_text(report_identity(report))
           << "\",\n"
           << "  \"success\":" << (report.success ? "true" : "false")
           << ",\n"
           << "  \"failure_reason\":\"" << escaped(report.failure_reason)
           << "\"\n}\n";
    return Result<std::string>::success(output.str());
}

auto canonical_json(const DesyncDiagnostic& diagnostic)
    -> Result<std::string> {
    std::ostringstream output;
    output << "{\n"
           << "  \"earliest_divergent_frame\":"
           << diagnostic.earliest_divergent_frame.value << ",\n"
           << "  \"local_hash\":\"" << hash_text(diagnostic.local_hash)
           << "\",\n"
           << "  \"remote_hash\":\"" << hash_text(diagnostic.remote_hash)
           << "\",\n"
           << "  \"recent_inputs\":[";
    for (std::size_t index = 0U; index < diagnostic.recent_inputs.size(); ++index) {
        if (index != 0U) {
            output << ',';
        }
        const auto& pair = diagnostic.recent_inputs[index];
        output << "{\"frame\":" << pair.a.frame.value
               << ",\"a\":" << static_cast<unsigned>(pair.a.buttons)
               << ",\"b\":" << static_cast<unsigned>(pair.b.buttons) << '}';
    }
    output << "],\n  \"state_summary\":{\"players\":[";
    for (std::size_t index = 0U; index < diagnostic.players.size(); ++index) {
        if (index != 0U) {
            output << ',';
        }
        const auto& player = diagnostic.players[index];
        output << "{\"id\":\"" << (player.id == PlayerId::a ? "A" : "B")
               << "\",\"x\":" << player.x << ",\"y\":" << player.y
               << ",\"hp\":" << player.hp << ",\"score\":"
               << player.score << '}';
    }
    output << "],\"active_projectiles\":" << diagnostic.active_projectiles
           << "},\n"
           << "  \"simulation_version\":" << diagnostic.simulation_version
           << ",\n  \"protocol_version\":" << diagnostic.protocol_version
           << ",\n  \"scenario_seed\":" << diagnostic.scenario_seed
           << "\n}\n";
    return Result<std::string>::success(output.str());
}

auto canonical_json(const Trace& trace) -> Result<std::string> {
    if (trace.frames.size() > kMaxTraceFrames ||
        trace.packets.size() > kMaxTracePacketEvents ||
        trace.rollbacks.size() > kMaxTraceRollbackEvents) {
        return Result<std::string>::failure(
            Error{ErrorCode::capacity_exceeded, trace.frames.size(), 0U,
                  "trace_capacity"});
    }
    std::ostringstream output;
    output << "{\"trace_version\":" << trace.trace_version
           << ",\"scenario_seed\":" << trace.scenario_seed
           << ",\"sample_interval\":" << trace.sample_interval
           << ",\"frames\":[";
    for (std::size_t index = 0U; index < trace.frames.size(); ++index) {
        if (index != 0U) {
            output << ',';
        }
        const auto& frame = trace.frames[index];
        output << "{\"frame\":" << frame.frame.value << ",\"players\":[";
        write_player(output, frame.state.players[0]);
        output << ',';
        write_player(output, frame.state.players[1]);
        output << "],\"projectiles\":[";
        bool first_projectile = true;
        for (const auto& projectile : frame.state.projectiles) {
            if (!projectile.active) {
                continue;
            }
            if (!first_projectile) {
                output << ',';
            }
            first_projectile = false;
            output << "{\"id\":" << projectile.stable_id
                   << ",\"owner\":\""
                   << (projectile.owner == PlayerId::a ? "A" : "B")
                   << "\",\"x\":" << projectile.x << ",\"y\":"
                   << projectile.y << '}';
        }
        output << "],\"hash\":\"" << hash_text(frame.hash)
               << "\",\"predicted\":" << (frame.predicted ? "true" : "false")
               << ",\"confirmed_frame\":" << frame.confirmed_frame.value << '}';
    }
    output << "],\"packets\":[";
    for (std::size_t index = 0U; index < trace.packets.size(); ++index) {
        if (index != 0U) {
            output << ',';
        }
        const auto& packet = trace.packets[index];
        output << "{\"tick\":" << packet.tick.value << ",\"kind\":\""
               << packet_kind_text(packet.kind) << "\",\"from\":\""
               << endpoint_text(packet.from) << "\",\"to\":\""
               << endpoint_text(packet.to) << "\",\"sequence\":"
               << packet.sequence << '}';
    }
    output << "],\"rollbacks\":[";
    for (std::size_t index = 0U; index < trace.rollbacks.size(); ++index) {
        if (index != 0U) {
            output << ',';
        }
        const auto& rollback = trace.rollbacks[index];
        output << "{\"observed_at\":" << rollback.observed_at.value
               << ",\"rollback_from\":" << rollback.rollback_from.value
               << ",\"depth\":" << rollback.depth << '}';
    }
    output << "],\"omitted_frame_samples\":" << trace.omitted_frame_samples
           << ",\"omitted_packet_events\":" << trace.omitted_packet_events
           << ",\"omitted_rollback_events\":"
           << trace.omitted_rollback_events;
    if (trace.desync.has_value()) {
        output << ",\"desync\":{\"frame\":" << trace.desync->frame.value
               << ",\"local_hash\":\"" << hash_text(trace.desync->local_hash)
               << "\",\"remote_hash\":\"" << hash_text(trace.desync->remote_hash)
               << "\"}";
    } else {
        output << ",\"desync\":null";
    }
    output << "}\n";
    return Result<std::string>::success(output.str());
}

}  // namespace rollback_lab
