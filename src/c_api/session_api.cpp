#include "internal.hpp"

#include <rollback_lab/version.hpp>

#include <algorithm>
#include <cstring>
#include <limits>

namespace rl_detail {
auto to_status(const rollback_lab::ErrorCode code) noexcept -> rl_status {
    using rollback_lab::ErrorCode;
    switch (code) {
    case ErrorCode::none: return RL_OK;
    case ErrorCode::invalid_argument: return RL_INVALID_ARGUMENT;
    case ErrorCode::frame_mismatch: return RL_INVALID_FRAME;
    case ErrorCode::stale_frame: return RL_STALE_FRAME;
    case ErrorCode::rollback_window_exceeded: return RL_ROLLBACK_WINDOW;
    case ErrorCode::unsupported_version: return RL_ABI_VERSION;
    case ErrorCode::capacity_exceeded:
    case ErrorCode::queue_overflow: return RL_CAPACITY;
    case ErrorCode::timeout: return RL_TIMEOUT;
    case ErrorCode::desync: return RL_DESYNC;
    case ErrorCode::replay_mismatch: return RL_REPLAY_MISMATCH;
    case ErrorCode::io_error: return RL_IO;
    default: return RL_INTERNAL_FAILURE;
    }
}

void copy_snapshot(const rollback_lab::WorldState& world, rl_world_snapshot& output) {
    auto copy = initialized<rl_world_snapshot>();
    copy.state_hash = rollback_lab::hash_canonical(world);
    copy.frame = world.frame.value;
    copy.next_projectile_id = world.next_projectile_id;
    copy.player_count = 2;
    copy.projectile_capacity = 64;
    for (std::size_t i = 0; i < world.players.size(); ++i) {
        const auto& player = world.players[i];
        copy.players[i] = {static_cast<std::uint32_t>(player.id), player.x, player.y,
                          player.velocity_x, player.velocity_y, player.hp,
                          player.facing_x, player.facing_y, player.score,
                          player.attack_cooldown, 0};
    }
    for (std::size_t i = 0; i < world.projectiles.size(); ++i) {
        const auto& projectile = world.projectiles[i];
        copy.projectiles[i] = {projectile.stable_id, static_cast<std::uint32_t>(projectile.owner),
                              projectile.x, projectile.y, projectile.velocity_x,
                              projectile.velocity_y, projectile.ttl,
                              static_cast<std::uint8_t>(projectile.active), 0, 0};
    }
    output = copy;
}
} // namespace rl_detail

extern "C" {
RL_API rl_status rl_get_version(rl_version_info* output) {
    return rl_detail::boundary([&]() -> rl_status {
        const auto status = rl_detail::validate(output);
        if (status != RL_OK) return status;
        auto result = rl_detail::initialized<rl_version_info>();
        result.sdk_major = 0; result.sdk_minor = 2; result.sdk_patch = 0;
        result.simulation_version = rollback_lab::kSimulationVersion;
        result.protocol_version = rollback_lab::kProtocolVersion;
        result.replay_version = rollback_lab::kReplayVersion;
        result.capabilities = RL_CAP_SESSION | RL_CAP_LIVE | RL_CAP_CANONICAL_BYTES | RL_CAP_UDP;
        const auto length = std::min(rollback_lab::kGitSha.size(), sizeof(result.source_git_sha) - 1);
        std::memcpy(result.source_git_sha, rollback_lab::kGitSha.data(), length);
        *output = result;
        return RL_OK;
    });
}

RL_API rl_status rl_session_create(const rl_session_config* config, rl_session** output) {
    return rl_detail::boundary([&]() -> rl_status {
        if (!output) return RL_INVALID_ARGUMENT;
        *output = nullptr;
        const auto status = rl_detail::validate(config);
        if (status != RL_OK) return status;
        if (config->local_peer > RL_PEER_B) return RL_INVALID_PEER;
        if (config->max_rollback_frames == 0 ||
            config->max_rollback_frames > rollback_lab::kDefaultMaxRollbackFrames ||
            config->simulation_variant > RL_VARIANT_DAMAGE_BIAS || config->reserved != 0)
            return RL_INVALID_ARGUMENT;
        rollback_lab::SessionConfig native;
        native.local_peer = static_cast<rollback_lab::PlayerId>(config->local_peer);
        native.max_rollback_frames = config->max_rollback_frames;
        native.variant = static_cast<rollback_lab::SimulationVariant>(config->simulation_variant);
        *output = new rl_session(native);
        return RL_OK;
    });
}

RL_API rl_status rl_session_destroy(rl_session* session) {
    return rl_detail::boundary([&]() -> rl_status {
        const auto status = rl_detail::check_session(session, true);
        if (status != RL_OK) return status;
        delete session;
        return RL_OK;
    });
}

RL_API rl_status rl_session_advance(rl_session* session, const rl_input_frame* input, rl_advance_result* output) {
    return rl_detail::boundary([&]() -> rl_status {
        auto status = rl_detail::check_session(session, true);
        if (status != RL_OK) return status;
        status = rl_detail::validate_input(input);
        if (status != RL_OK) return status;
        status = rl_detail::validate(output);
        if (status != RL_OK) return status;
        if (input->peer != session->local_peer) return RL_INVALID_PEER;
        if (input->frame != session->native.report().state.frame.value) return RL_INVALID_FRAME;
        const auto result = session->native.advance(rl_detail::input(*input));
        if (!result.ok()) return rl_detail::to_status(result.error().code);
        session->touched = true;
        auto copy = rl_detail::initialized<rl_advance_result>();
        copy.state_hash = result.value().state_hash;
        copy.simulated_frame = result.value().simulated_frame.value;
        copy.predicted_remote = static_cast<std::uint32_t>(result.value().predicted_remote);
        *output = copy;
        return RL_OK;
    });
}

RL_API rl_status rl_session_ingest_remote(rl_session* session, const rl_input_frame* input) {
    return rl_detail::boundary([&]() -> rl_status {
        auto status = rl_detail::check_session(session, true);
        if (status != RL_OK) return status;
        status = rl_detail::validate_input(input);
        if (status != RL_OK) return status;
        if (input->peer == session->local_peer) return RL_INVALID_PEER;
        const auto current = session->native.report().state.frame;
        const rollback_lab::FrameNumber remote{input->frame};
        if (remote != current && !rollback_lab::frame_before(remote, current))
            return RL_INVALID_FRAME;
        const auto result = session->native.ingest_remote(rl_detail::input(*input));
        if (result.ok()) session->touched = true;
        return result.ok() ? RL_OK : rl_detail::to_status(result.error().code);
    });
}

RL_API rl_status rl_session_flush_corrections(rl_session* session, rl_correction_result* output) {
    return rl_detail::boundary([&]() -> rl_status {
        auto status = rl_detail::check_session(session, true);
        if (status != RL_OK) return status;
        status = rl_detail::validate(output);
        if (status != RL_OK) return status;
        const auto result = session->native.flush_corrections();
        if (!result.ok()) return rl_detail::to_status(result.error().code);
        if (result.value().performed) session->touched = true;
        auto copy = rl_detail::initialized<rl_correction_result>();
        copy.performed = static_cast<std::uint32_t>(result.value().performed);
        copy.rollback_from = result.value().rollback_from.value;
        copy.resimulated_frames = result.value().resimulated_frames;
        *output = copy;
        return RL_OK;
    });
}

RL_API rl_status rl_session_get_snapshot(rl_session* session, rl_world_snapshot* output) {
    return rl_detail::boundary([&]() -> rl_status {
        auto status = rl_detail::check_session(session);
        if (status != RL_OK) return status;
        status = rl_detail::validate(output);
        if (status != RL_OK) return status;
        rl_detail::copy_snapshot(session->native.report().state, *output);
        return RL_OK;
    });
}

RL_API rl_status rl_session_get_confirmed_frame(rl_session* session, rl_frame_result* output) {
    return rl_detail::boundary([&]() -> rl_status {
        auto status = rl_detail::check_session(session);
        if (status != RL_OK) return status;
        status = rl_detail::validate(output);
        if (status != RL_OK) return status;
        auto result = rl_detail::initialized<rl_frame_result>();
        result.frame = session->native.report().metrics.confirmed_frame.value;
        *output = result;
        return RL_OK;
    });
}

RL_API rl_status rl_session_get_metrics(rl_session* session, rl_metrics* output) {
    return rl_detail::boundary([&]() -> rl_status {
        auto status = rl_detail::check_session(session);
        if (status != RL_OK) return status;
        status = rl_detail::validate(output);
        if (status != RL_OK) return status;
        const auto metrics = session->native.report().metrics;
        auto result = rl_detail::initialized<rl_metrics>();
        result.total_resimulated_frames = metrics.total_resimulated_frames;
        result.predicted_input_count = metrics.predicted_input_count;
        result.late_input_count = metrics.late_input_count;
        result.rollback_count = metrics.rollback_count;
        result.maximum_rollback_depth = metrics.maximum_rollback_depth;
        result.confirmed_frame = metrics.confirmed_frame.value;
        *output = result;
        return RL_OK;
    });
}

RL_API rl_status rl_session_get_hash(rl_session* session, rl_hash_result* output) {
    return rl_detail::boundary([&]() -> rl_status {
        auto status = rl_detail::check_session(session);
        if (status != RL_OK) return status;
        status = rl_detail::validate(output);
        if (status != RL_OK) return status;
        const auto report = session->native.report();
        auto result = rl_detail::initialized<rl_hash_result>();
        result.frame = report.state.frame.value;
        result.state_hash = report.final_hash;
        *output = result;
        return RL_OK;
    });
}

RL_API rl_status rl_session_hash_at(rl_session* session, uint32_t boundary, rl_hash_result* output) {
    return rl_detail::boundary([&]() -> rl_status {
        auto status = rl_detail::check_session(session);
        if (status != RL_OK) return status;
        status = rl_detail::validate(output);
        if (status != RL_OK) return status;
        const auto result = session->native.hash_at({boundary});
        if (!result.ok()) return rl_detail::to_status(result.error().code);
        auto copy = rl_detail::initialized<rl_hash_result>();
        copy.frame = boundary;
        copy.state_hash = result.value();
        *output = copy;
        return RL_OK;
    });
}

RL_API rl_status rl_session_serialize_state(rl_session* session, uint8_t* buffer, uint32_t capacity, uint32_t* required) {
    return rl_detail::boundary([&]() -> rl_status {
        const auto status = rl_detail::check_session(session);
        if (status != RL_OK) return status;
        if (!required || (!buffer && capacity != 0)) return RL_INVALID_ARGUMENT;
        const auto bytes = rollback_lab::serialize_canonical(session->native.report().state);
        if (bytes.size() > std::numeric_limits<std::uint32_t>::max()) return RL_CAPACITY;
        *required = static_cast<std::uint32_t>(bytes.size());
        if (capacity < *required) return RL_BUFFER_TOO_SMALL;
        std::memcpy(buffer, bytes.data(), bytes.size());
        return RL_OK;
    });
}
} // extern "C"
