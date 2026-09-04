#ifndef ROLLBACK_LAB_C_H
#define ROLLBACK_LAB_C_H

#include <stdint.h>

#if defined(_WIN32)
# if defined(ROLLBACK_LAB_C_EXPORTS)
#  define RL_API __declspec(dllexport)
# else
#  define RL_API __declspec(dllimport)
# endif
#else
# define RL_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define RL_API_VERSION UINT32_C(1)
#define RL_PEER_A UINT32_C(0)
#define RL_PEER_B UINT32_C(1)
#define RL_VARIANT_CANONICAL UINT32_C(0)
#define RL_VARIANT_DAMAGE_BIAS UINT32_C(1)
#define RL_BUTTON_UP UINT8_C(1)
#define RL_BUTTON_DOWN UINT8_C(2)
#define RL_BUTTON_LEFT UINT8_C(4)
#define RL_BUTTON_RIGHT UINT8_C(8)
#define RL_BUTTON_ATTACK UINT8_C(16)
#define RL_CAP_SESSION UINT64_C(1)
#define RL_CAP_LIVE UINT64_C(2)
#define RL_CAP_CANONICAL_BYTES UINT64_C(4)
#define RL_CAP_UDP UINT64_C(8)

/* Values are uint32_t, never a compiler-dependent enum representation. */
typedef uint32_t rl_status;
#define RL_OK UINT32_C(0)
#define RL_INVALID_ARGUMENT UINT32_C(1)
#define RL_ABI_VERSION UINT32_C(2)
#define RL_STRUCT_SIZE UINT32_C(3)
#define RL_INVALID_PEER UINT32_C(4)
#define RL_INVALID_FRAME UINT32_C(5)
#define RL_STALE_FRAME UINT32_C(6)
#define RL_ROLLBACK_WINDOW UINT32_C(7)
#define RL_BUFFER_TOO_SMALL UINT32_C(8)
#define RL_INTERNAL_FAILURE UINT32_C(9)
#define RL_WRONG_THREAD UINT32_C(10)
#define RL_BORROWED UINT32_C(11)
#define RL_CAPACITY UINT32_C(12)
#define RL_TIMEOUT UINT32_C(13)
#define RL_DESYNC UINT32_C(14)
#define RL_IO UINT32_C(15)
#define RL_REPLAY_MISMATCH UINT32_C(16)
#define RL_NETWORK_VERSION UINT32_C(17)
#define RL_HANDSHAKE_PROFILE UINT32_C(18)
#define RL_PACKET UINT32_C(19)

/* ABI v1 targets x64 little endian with natural alignment, maximum 8 bytes.
 * No packing pragma is required or permitted around this header. Structs passed
 * to functions must be zero-initialized, then set api_version=RL_API_VERSION
 * and struct_size=sizeof(the exact type). v1 requires exact size; incompatible
 * extensions require a new ABI version. Input reserved bytes must be zero.
 * Nested snapshot records inherit the enclosing snapshot's version.
 *
 * SDK owns opaque resources; callers own all output storage. Every handle is
 * affine to the creating thread, including queries and destruction. Concurrent
 * calls are unsupported. Destroy exactly once on that thread; a stale, forged
 * handle or invalid storage pointer is outside the contract. No exception or
 * allocator ownership crosses this boundary. Sessions start at default frame 0.
 * Logical frame arithmetic follows the core's uint32 wrap semantics.
 */
typedef struct rl_session rl_session;
typedef struct rl_live rl_live;

typedef struct rl_session_config {
    uint32_t api_version;
    uint32_t struct_size;
    uint32_t local_peer;
    uint32_t max_rollback_frames; /* 1..120 */
    uint32_t simulation_variant;
    uint32_t reserved;
} rl_session_config;

typedef struct rl_input_frame {
    uint32_t api_version;
    uint32_t struct_size;
    uint32_t frame;
    uint32_t sequence;
    uint32_t peer;
    uint8_t buttons;
    uint8_t reserved[3];
} rl_input_frame;

typedef struct rl_player_snapshot {
    uint32_t peer;
    int32_t x;
    int32_t y;
    int32_t velocity_x;
    int32_t velocity_y;
    int16_t hp;
    int8_t facing_x;
    int8_t facing_y;
    uint16_t score;
    uint16_t attack_cooldown;
    uint32_t reserved;
} rl_player_snapshot;

typedef struct rl_projectile_snapshot {
    uint32_t stable_id;
    uint32_t owner;
    int32_t x;
    int32_t y;
    int32_t velocity_x;
    int32_t velocity_y;
    uint16_t ttl;
    uint8_t active;
    uint8_t reserved_byte;
    uint32_t reserved;
} rl_projectile_snapshot;

typedef struct rl_world_snapshot {
    uint32_t api_version;
    uint32_t struct_size;
    uint64_t state_hash;
    uint32_t frame; /* current boundary; coordinates are 1/1024 world units */
    uint32_t next_projectile_id;
    uint32_t player_count;
    uint32_t projectile_capacity;
    rl_player_snapshot players[2];
    rl_projectile_snapshot projectiles[64];
} rl_world_snapshot;

typedef struct rl_advance_result {
    uint32_t api_version;
    uint32_t struct_size;
    uint64_t state_hash;
    uint32_t simulated_frame;
    uint32_t predicted_remote;
} rl_advance_result;

typedef struct rl_correction_result {
    uint32_t api_version;
    uint32_t struct_size;
    uint32_t performed;
    uint32_t rollback_from;
    uint32_t resimulated_frames;
    uint32_t reserved;
} rl_correction_result;

typedef struct rl_metrics {
    uint32_t api_version;
    uint32_t struct_size;
    uint64_t total_resimulated_frames;
    uint64_t predicted_input_count;
    uint64_t late_input_count;
    uint32_t rollback_count;
    uint32_t maximum_rollback_depth;
    uint32_t confirmed_frame;
    uint32_t reserved;
} rl_metrics;

typedef struct rl_frame_result {
    uint32_t api_version;
    uint32_t struct_size;
    uint32_t frame;
    uint32_t reserved;
} rl_frame_result;

typedef struct rl_hash_result {
    uint32_t api_version;
    uint32_t struct_size;
    uint64_t state_hash;
    uint32_t frame;
    uint32_t reserved;
} rl_hash_result;

typedef struct rl_version_info {
    uint32_t api_version;
    uint32_t struct_size;
    uint32_t sdk_major;
    uint32_t sdk_minor;
    uint32_t sdk_patch;
    uint32_t simulation_version;
    uint32_t protocol_version;
    uint32_t replay_version;
    uint64_t capabilities;
    char source_git_sha[41]; /* 40 hex digits plus NUL, or "unavailable" */
    uint8_t reserved[7];
} rl_version_info;

RL_API rl_status rl_get_version(rl_version_info* output);
RL_API rl_status rl_session_create(const rl_session_config* config, rl_session** output);
RL_API rl_status rl_session_destroy(rl_session* session);
RL_API rl_status rl_session_advance(rl_session* session, const rl_input_frame* input, rl_advance_result* output);
/* Accept current or retained past frames. Caller queues future inputs until
 * their frame is current; future frames return INVALID_FRAME without mutation. */
RL_API rl_status rl_session_ingest_remote(rl_session* session, const rl_input_frame* input);
RL_API rl_status rl_session_flush_corrections(rl_session* session, rl_correction_result* output);
RL_API rl_status rl_session_get_snapshot(rl_session* session, rl_world_snapshot* output);
RL_API rl_status rl_session_get_confirmed_frame(rl_session* session, rl_frame_result* output);
RL_API rl_status rl_session_get_metrics(rl_session* session, rl_metrics* output);
RL_API rl_status rl_session_get_hash(rl_session* session, rl_hash_result* output);
/* Only retained, confirmed boundaries can be queried; otherwise STALE_FRAME. */
RL_API rl_status rl_session_hash_at(rl_session* session, uint32_t boundary, rl_hash_result* output);
/* Sizing: NULL,0 writes required and returns BUFFER_TOO_SMALL. No bytes are
 * written on insufficient capacity. All serialized bytes use canonical v1. */
RL_API rl_status rl_session_serialize_state(rl_session* session, uint8_t* buffer, uint32_t capacity, uint32_t* required);

typedef struct rl_live_config {
    uint32_t api_version;
    uint32_t struct_size;
    uint64_t scenario_seed;
    uint64_t transport_seed;
    uint32_t frame_count;
    uint32_t base_latency_ticks;
    uint32_t jitter_ticks;
    uint32_t loss_percent;
    uint32_t reorder_percent;
    uint32_t duplicate_percent;
    uint32_t burst_loss_percent;
    uint32_t max_queue_packets;
    uint32_t max_queue_bytes;
    uint32_t bandwidth_bytes_per_tick;
    uint32_t max_packet_age_ticks;
    uint32_t tail_redundancy_ticks;
} rl_live_config;

typedef struct rl_live_step_result {
    uint32_t api_version;
    uint32_t struct_size;
    uint32_t logical_tick;
    uint32_t finished;
    uint32_t desync_detected;
    uint32_t earliest_divergent_frame;
} rl_live_step_result;

typedef struct rl_live_correction {
    uint32_t api_version;
    uint32_t struct_size;
    uint32_t performed;
    uint32_t rollback_from;
    uint32_t resimulated_frames;
    uint32_t reserved;
    rl_world_snapshot before;
    rl_world_snapshot after;
} rl_live_correction;

/* Driver borrows two distinct unused frame-0 sessions A and B. While borrowed,
 * session mutators and destruction return BORROWED; queries remain available.
 * Destroy driver first, then sessions, all on the creating thread.
 * override_local_input is 0 (scripted A) or 1 (sample local_buttons for A).
 * Report/replay require completed logical execution (including controlled
 * desync demonstrations); trace is also available on transport/session failure.
 * String sizing counts the NUL. NULL,0 returns BUFFER_TOO_SMALL with required.
 * Live facade limits: 1..36000 simulation frames; 0..4096 tail ticks followed
 * by 32 drain ticks; latency/jitter <=10000 ticks; each percent <=100;
 * 1..65536 queue packets; 1..64 MiB queue bytes and bandwidth per tick;
 * 1..100000 maximum packet age ticks. Core session limits remain unchanged.
 */
RL_API rl_status rl_live_create(const rl_live_config* config, rl_session* a, rl_session* b, rl_live** output);
RL_API rl_status rl_live_destroy(rl_live* live);
RL_API rl_status rl_live_step(rl_live* live, uint32_t override_local_input, uint32_t local_buttons, rl_live_step_result* output);
RL_API rl_status rl_live_get_correction(rl_live* live, uint32_t peer, rl_live_correction* output);
RL_API rl_status rl_live_copy_report(rl_live* live, char* buffer, uint32_t capacity, uint32_t* required);
RL_API rl_status rl_live_copy_trace(rl_live* live, char* buffer, uint32_t capacity, uint32_t* required);
RL_API rl_status rl_live_copy_replay(rl_live* live, uint8_t* buffer, uint32_t capacity, uint32_t* required);

typedef struct rl_udp_peer rl_udp_peer;
#define RL_UDP_HANDSHAKE UINT32_C(0)
#define RL_UDP_RUNNING UINT32_C(1)
#define RL_UDP_CONFIRMING UINT32_C(2)
#define RL_UDP_FINISHED UINT32_C(3)
#define RL_UDP_FAILED UINT32_C(4)

typedef struct rl_udp_config {
    uint32_t api_version;
    uint32_t struct_size;
    uint64_t scenario_seed;
    uint64_t transport_seed;
    uint32_t frame_count;
    uint32_t listen_port;
    uint32_t relay_port;
    uint32_t handshake_timeout_ms;
    uint32_t run_timeout_ms;
    uint32_t advertised_protocol_version;
    uint32_t advertised_simulation_version;
    uint32_t advertised_abi_profile_version;
    uint32_t reserved[3];
} rl_udp_config;

typedef struct rl_udp_step_result {
    uint32_t api_version;
    uint32_t struct_size;
    uint32_t logical_tick;
    uint32_t phase;
    uint32_t finished;
    uint32_t handshake_complete;
    uint32_t desync_detected;
    uint32_t earliest_divergent_frame;
} rl_udp_step_result;

/* Borrows one unused frame-zero session; queries remain available. Destroy the
 * UDP driver before its session, on the creating thread. No worker threads.
 * Each step polls at most 64 datagrams and advances at most one scripted local
 * frame. elapsed_milliseconds is caller-provided, monotonic elapsed time since
 * creation; it controls transport deadlines only. Call at fixed-step cadence.
 * Bounds: 1..240 frames, 1..60000 ms handshake/run, nonzero relay port.
 * listen_port=0 requests a dynamic port; copy_failure reports the bound port.
 * Advertised version fields: zero means the compiled version. Nonzero values
 * are negative-test advertisements, never expected-version overrides.
 * Protocol v1 wire format is unchanged. The named engine-udp-v1 configuration
 * digest binds ABI, protocol, simulation, both seeds and target frame. ABI is
 * validated as part of this aggregate profile, not a separate remote field.
 * Report/replay require successful completion. Failure JSON remains readable
 * at every phase and contains any confirmed desync diagnostic. Sized copies
 * include the NUL for JSON, exclude it for replay, and never partially write.
 * Successful terminal steps are idempotent; failed steps retain their error.
 */
RL_API rl_status rl_udp_peer_create(const rl_udp_config* config, rl_session* session, rl_udp_peer** output);
RL_API rl_status rl_udp_peer_destroy(rl_udp_peer* peer);
RL_API rl_status rl_udp_peer_step(rl_udp_peer* peer, uint64_t elapsed_milliseconds, rl_udp_step_result* output);
RL_API rl_status rl_udp_peer_get_correction(rl_udp_peer* peer, rl_live_correction* output);
RL_API rl_status rl_udp_peer_copy_report(rl_udp_peer* peer, char* buffer, uint32_t capacity, uint32_t* required);
RL_API rl_status rl_udp_peer_copy_replay(rl_udp_peer* peer, uint8_t* buffer, uint32_t capacity, uint32_t* required);
RL_API rl_status rl_udp_peer_copy_failure(rl_udp_peer* peer, char* buffer, uint32_t capacity, uint32_t* required);

#ifdef __cplusplus
}
#endif
#endif
