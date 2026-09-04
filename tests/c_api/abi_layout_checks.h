#ifndef ROLLBACK_LAB_ABI_LAYOUT_CHECKS_H
#define ROLLBACK_LAB_ABI_LAYOUT_CHECKS_H
#include <rollback_lab/c_api/rollback_lab_c.h>
#include <stddef.h>
#ifdef __cplusplus
#define RL_LAYOUT_ASSERT(value) static_assert(value, #value)
#define RL_ALIGNOF(type) alignof(type)
#else
#define RL_LAYOUT_ASSERT(value) _Static_assert(value, #value)
#define RL_ALIGNOF(type) _Alignof(type)
#endif
#define RL_OFFSET(type, field, offset) RL_LAYOUT_ASSERT(offsetof(type, field) == offset)
#define RL_LAYOUT(type, size, alignment) \
    RL_LAYOUT_ASSERT(sizeof(type) == size); \
    RL_LAYOUT_ASSERT(RL_ALIGNOF(type) == alignment)
#define RL_HEADER(type) RL_OFFSET(type, api_version, 0); RL_OFFSET(type, struct_size, 4)
RL_LAYOUT(rl_status, 4, 4);
RL_LAYOUT(rl_session_config, 24, 4); RL_HEADER(rl_session_config);
RL_OFFSET(rl_session_config, local_peer, 8); RL_OFFSET(rl_session_config, max_rollback_frames, 12);
RL_OFFSET(rl_session_config, simulation_variant, 16); RL_OFFSET(rl_session_config, reserved, 20);
RL_LAYOUT(rl_input_frame, 24, 4); RL_HEADER(rl_input_frame);
RL_OFFSET(rl_input_frame, frame, 8); RL_OFFSET(rl_input_frame, sequence, 12);
RL_OFFSET(rl_input_frame, peer, 16); RL_OFFSET(rl_input_frame, buttons, 20); RL_OFFSET(rl_input_frame, reserved, 21);
RL_LAYOUT(rl_player_snapshot, 32, 4);
RL_OFFSET(rl_player_snapshot, peer, 0); RL_OFFSET(rl_player_snapshot, x, 4); RL_OFFSET(rl_player_snapshot, y, 8);
RL_OFFSET(rl_player_snapshot, velocity_x, 12); RL_OFFSET(rl_player_snapshot, velocity_y, 16);
RL_OFFSET(rl_player_snapshot, hp, 20); RL_OFFSET(rl_player_snapshot, facing_x, 22); RL_OFFSET(rl_player_snapshot, facing_y, 23);
RL_OFFSET(rl_player_snapshot, score, 24); RL_OFFSET(rl_player_snapshot, attack_cooldown, 26); RL_OFFSET(rl_player_snapshot, reserved, 28);
RL_LAYOUT(rl_projectile_snapshot, 32, 4);
RL_OFFSET(rl_projectile_snapshot, stable_id, 0); RL_OFFSET(rl_projectile_snapshot, owner, 4);
RL_OFFSET(rl_projectile_snapshot, x, 8); RL_OFFSET(rl_projectile_snapshot, y, 12);
RL_OFFSET(rl_projectile_snapshot, velocity_x, 16); RL_OFFSET(rl_projectile_snapshot, velocity_y, 20);
RL_OFFSET(rl_projectile_snapshot, ttl, 24); RL_OFFSET(rl_projectile_snapshot, active, 26);
RL_OFFSET(rl_projectile_snapshot, reserved_byte, 27); RL_OFFSET(rl_projectile_snapshot, reserved, 28);
RL_LAYOUT(rl_world_snapshot, 2144, 8); RL_HEADER(rl_world_snapshot);
RL_OFFSET(rl_world_snapshot, state_hash, 8); RL_OFFSET(rl_world_snapshot, frame, 16);
RL_OFFSET(rl_world_snapshot, next_projectile_id, 20); RL_OFFSET(rl_world_snapshot, player_count, 24);
RL_OFFSET(rl_world_snapshot, projectile_capacity, 28); RL_OFFSET(rl_world_snapshot, players, 32);
RL_OFFSET(rl_world_snapshot, projectiles, 96);
RL_LAYOUT(rl_advance_result, 24, 8); RL_HEADER(rl_advance_result);
RL_OFFSET(rl_advance_result, state_hash, 8); RL_OFFSET(rl_advance_result, simulated_frame, 16);
RL_OFFSET(rl_advance_result, predicted_remote, 20);
RL_LAYOUT(rl_correction_result, 24, 4); RL_HEADER(rl_correction_result);
RL_OFFSET(rl_correction_result, performed, 8); RL_OFFSET(rl_correction_result, rollback_from, 12);
RL_OFFSET(rl_correction_result, resimulated_frames, 16); RL_OFFSET(rl_correction_result, reserved, 20);
RL_LAYOUT(rl_metrics, 48, 8); RL_HEADER(rl_metrics);
RL_OFFSET(rl_metrics, total_resimulated_frames, 8); RL_OFFSET(rl_metrics, predicted_input_count, 16);
RL_OFFSET(rl_metrics, late_input_count, 24); RL_OFFSET(rl_metrics, rollback_count, 32);
RL_OFFSET(rl_metrics, maximum_rollback_depth, 36); RL_OFFSET(rl_metrics, confirmed_frame, 40); RL_OFFSET(rl_metrics, reserved, 44);
RL_LAYOUT(rl_frame_result, 16, 4); RL_HEADER(rl_frame_result);
RL_OFFSET(rl_frame_result, frame, 8); RL_OFFSET(rl_frame_result, reserved, 12);
RL_LAYOUT(rl_hash_result, 24, 8); RL_HEADER(rl_hash_result);
RL_OFFSET(rl_hash_result, state_hash, 8); RL_OFFSET(rl_hash_result, frame, 16); RL_OFFSET(rl_hash_result, reserved, 20);
RL_LAYOUT(rl_version_info, 88, 8); RL_HEADER(rl_version_info);
RL_OFFSET(rl_version_info, sdk_major, 8); RL_OFFSET(rl_version_info, sdk_minor, 12); RL_OFFSET(rl_version_info, sdk_patch, 16);
RL_OFFSET(rl_version_info, simulation_version, 20); RL_OFFSET(rl_version_info, protocol_version, 24); RL_OFFSET(rl_version_info, replay_version, 28);
RL_OFFSET(rl_version_info, capabilities, 32); RL_OFFSET(rl_version_info, source_git_sha, 40); RL_OFFSET(rl_version_info, reserved, 81);
RL_LAYOUT(rl_live_config, 72, 8); RL_HEADER(rl_live_config);
RL_OFFSET(rl_live_config, scenario_seed, 8); RL_OFFSET(rl_live_config, transport_seed, 16);
RL_OFFSET(rl_live_config, frame_count, 24); RL_OFFSET(rl_live_config, base_latency_ticks, 28);
RL_OFFSET(rl_live_config, jitter_ticks, 32); RL_OFFSET(rl_live_config, loss_percent, 36);
RL_OFFSET(rl_live_config, reorder_percent, 40); RL_OFFSET(rl_live_config, duplicate_percent, 44);
RL_OFFSET(rl_live_config, burst_loss_percent, 48); RL_OFFSET(rl_live_config, max_queue_packets, 52);
RL_OFFSET(rl_live_config, max_queue_bytes, 56); RL_OFFSET(rl_live_config, bandwidth_bytes_per_tick, 60);
RL_OFFSET(rl_live_config, max_packet_age_ticks, 64); RL_OFFSET(rl_live_config, tail_redundancy_ticks, 68);
RL_LAYOUT(rl_live_step_result, 24, 4); RL_HEADER(rl_live_step_result);
RL_OFFSET(rl_live_step_result, logical_tick, 8); RL_OFFSET(rl_live_step_result, finished, 12);
RL_OFFSET(rl_live_step_result, desync_detected, 16); RL_OFFSET(rl_live_step_result, earliest_divergent_frame, 20);
RL_LAYOUT(rl_live_correction, 4312, 8); RL_HEADER(rl_live_correction);
RL_OFFSET(rl_live_correction, performed, 8); RL_OFFSET(rl_live_correction, rollback_from, 12);
RL_OFFSET(rl_live_correction, resimulated_frames, 16); RL_OFFSET(rl_live_correction, reserved, 20);
RL_OFFSET(rl_live_correction, before, 24); RL_OFFSET(rl_live_correction, after, 2168);
#undef RL_HEADER
#undef RL_LAYOUT
#undef RL_OFFSET
#undef RL_LAYOUT_ASSERT
#undef RL_ALIGNOF
#endif
