#include "internal.hpp"
#include <cstring>
#include <limits>
#include <memory>
#include <rollback_lab/udp/peer.hpp>
#include <span>
#include <string>
#include <string_view>

struct rl_udp_peer final {
    rl_session* session;
    std::unique_ptr<rollback_lab::PeerDriver> native;
    rl_status failure{RL_OK};
    rl_udp_step_result observation{rl_detail::initialized<rl_udp_step_result>()};
};
namespace {
using namespace rollback_lab;
using namespace rl_detail;
auto check(const rl_udp_peer* peer) -> rl_status {
    return peer ? check_session(peer->session) : RL_INVALID_ARGUMENT;
}
auto network_status(const Error& error) -> rl_status {
    const std::string_view context{error.context};
    if (context == "udp_engine_profile")
        return RL_HANDSHAKE_PROFILE;
    if (error.code == ErrorCode::unsupported_version)
        return RL_NETWORK_VERSION;
    if (context == "udp_relay_source" || context == "peer_packet_identity" ||
        context == "udp_hello_identity")
        return RL_PACKET;
    switch (error.code) {
    case ErrorCode::invalid_magic:
    case ErrorCode::unknown_packet_type:
    case ErrorCode::truncated_data:
    case ErrorCode::invalid_length:
    case ErrorCode::integrity_mismatch:
        return RL_PACKET;
    default:
        return to_status(error.code);
    }
}
auto validate_config(const rl_udp_config* c) -> rl_status {
    if (const auto s = validate(c); s != RL_OK)
        return s;
    if (!c->frame_count || c->frame_count > 240U || c->listen_port > 65535U || !c->relay_port ||
        c->relay_port > 65535U || c->listen_port == c->relay_port || !c->handshake_timeout_ms ||
        c->handshake_timeout_ms > 60000U || !c->run_timeout_ms || c->run_timeout_ms > 60000U ||
        c->advertised_protocol_version > 65535U || c->reserved[0] || c->reserved[1] ||
        c->reserved[2])
        return RL_INVALID_ARGUMENT;
    return RL_OK;
}
auto copy(std::span<const std::byte> bytes, void* buffer, uint32_t capacity, uint32_t* required)
    -> rl_status {
    if (!required || (!buffer && capacity))
        return RL_INVALID_ARGUMENT;
    if (bytes.size() > std::numeric_limits<uint32_t>::max())
        return RL_CAPACITY;
    *required = static_cast<uint32_t>(bytes.size());
    if (!buffer || capacity < *required)
        return RL_BUFFER_TOO_SMALL;
    std::memcpy(buffer, bytes.data(), bytes.size());
    return RL_OK;
}
auto copy(const std::string& text, char* buffer, uint32_t capacity, uint32_t* required)
    -> rl_status {
    return copy(std::as_bytes(std::span{text.c_str(), text.size() + 1U}), buffer, capacity,
                required);
}
void observe(rl_udp_peer& peer) {
    const auto native = peer.native->observation();
    peer.observation.logical_tick = native.logical_tick;
    peer.observation.phase = static_cast<uint32_t>(native.phase);
    peer.observation.finished = native.phase == PeerPhase::finished ? 1U : 0U;
    peer.observation.handshake_complete = native.handshake_complete ? 1U : 0U;
}
} // namespace
extern "C" {
rl_status rl_udp_peer_create(const rl_udp_config* config, rl_session* session,
                             rl_udp_peer** output) {
    return boundary([&]() -> rl_status {
        if (!output)
            return RL_INVALID_ARGUMENT;
        *output = nullptr;
        if (const auto s = validate_config(config); s != RL_OK)
            return s;
        if (const auto s = check_session(session, true); s != RL_OK)
            return s;
        if (session->touched)
            return RL_INVALID_FRAME;
        PeerConfig c;
        c.id = static_cast<PlayerId>(session->local_peer);
        c.scenario_seed = config->scenario_seed;
        c.transport_seed = config->transport_seed;
        c.frame_count = config->frame_count;
        c.listen_port = static_cast<uint16_t>(config->listen_port);
        c.relay_port = static_cast<uint16_t>(config->relay_port);
        c.handshake_timeout_milliseconds = config->handshake_timeout_ms;
        c.run_timeout_milliseconds = config->run_timeout_ms;
        c.protocol_version_override = static_cast<uint16_t>(
            config->advertised_protocol_version ? config->advertised_protocol_version
                                                : kProtocolVersion);
        c.simulation_version_override = config->advertised_simulation_version
                                            ? config->advertised_simulation_version
                                            : kSimulationVersion;
        c.engine_abi_version = RL_API_VERSION;
        c.advertised_abi_version = config->advertised_abi_profile_version;
        auto created = PeerDriver::create(c, session->native);
        if (!created.ok())
            return network_status(created.error());
        auto peer = std::make_unique<rl_udp_peer>(rl_udp_peer{session, std::move(created.value())});
        session->borrowed = true;
        *output = peer.release();
        return RL_OK;
    });
}
rl_status rl_udp_peer_destroy(rl_udp_peer* peer) {
    return boundary([&]() -> rl_status {
        if (const auto s = check(peer); s != RL_OK)
            return s;
        peer->native.reset();
        peer->session->borrowed = false;
        delete peer;
        return RL_OK;
    });
}
rl_status rl_udp_peer_step(rl_udp_peer* peer, uint64_t elapsed, rl_udp_step_result* output) {
    return boundary([&]() -> rl_status {
        if (const auto s = check(peer); s != RL_OK)
            return s;
        if (const auto s = validate(output); s != RL_OK)
            return s;
        if (peer->failure != RL_OK) {
            *output = peer->observation;
            return peer->failure;
        }
        peer->failure = RL_INTERNAL_FAILURE;
        const auto result = peer->native->step(elapsed);
        if (!result.ok()) {
            const auto s = network_status(result.error());
            if (std::string_view{result.error().context} == "udp_clock_reversed") {
                peer->failure = RL_OK;
                return s;
            }
            peer->failure = s;
            observe(*peer);
            peer->observation.desync_detected = s == RL_DESYNC ? 1U : 0U;
            peer->observation.earliest_divergent_frame =
                s == RL_DESYNC ? static_cast<uint32_t>(result.error().offset) : 0U;
            *output = peer->observation;
            return s;
        }
        peer->failure = RL_OK;
        peer->session->touched = true;
        observe(*peer);
        *output = peer->observation;
        return RL_OK;
    });
}
rl_status rl_udp_peer_get_correction(rl_udp_peer* peer, rl_live_correction* output) {
    return boundary([&]() -> rl_status {
        if (const auto s = check(peer); s != RL_OK)
            return s;
        if (const auto s = validate(output); s != RL_OK)
            return s;
        const auto& c = peer->native->correction();
        auto out = initialized<rl_live_correction>();
        out.performed = c.result.performed ? 1U : 0U;
        out.rollback_from = c.result.rollback_from.value;
        out.resimulated_frames = c.result.resimulated_frames;
        copy_snapshot(c.before, out.before);
        copy_snapshot(c.after, out.after);
        *output = out;
        return RL_OK;
    });
}
rl_status rl_udp_peer_copy_report(rl_udp_peer* peer, char* buffer, uint32_t capacity,
                                  uint32_t* required) {
    return boundary([&]() -> rl_status {
        if (const auto s = check(peer); s != RL_OK)
            return s;
        if (peer->native->observation().phase != PeerPhase::finished)
            return RL_INVALID_FRAME;
        return copy(peer->native->report_json(), buffer, capacity, required);
    });
}
rl_status rl_udp_peer_copy_replay(rl_udp_peer* peer, uint8_t* buffer, uint32_t capacity,
                                  uint32_t* required) {
    return boundary([&]() -> rl_status {
        if (const auto s = check(peer); s != RL_OK)
            return s;
        if (peer->native->observation().phase != PeerPhase::finished)
            return RL_INVALID_FRAME;
        return copy(std::span<const std::byte>{peer->native->replay_bytes()}, buffer, capacity,
                    required);
    });
}
rl_status rl_udp_peer_copy_failure(rl_udp_peer* peer, char* buffer, uint32_t capacity,
                                   uint32_t* required) {
    return boundary([&]() -> rl_status {
        if (const auto s = check(peer); s != RL_OK)
            return s;
        const auto json = peer->native->failure_json();
        const auto enriched =
            "{\"sdk_status\":" + std::to_string(peer->failure) + "," + json.substr(1);
        return copy(enriched, buffer, capacity, required);
    });
}
}
