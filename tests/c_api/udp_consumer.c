#include <rollback_lab/c_api/rollback_lab_c.h>
#include "abi_layout_checks.h"
#include <stddef.h>
#include <string.h>

_Static_assert(sizeof(rl_udp_config) == 72, "udp config layout");
_Static_assert(offsetof(rl_udp_config, frame_count) == 24, "udp config fields");
_Static_assert(sizeof(rl_udp_step_result) == 32, "udp step layout");

int rl_udp_c11_checks(void) {
    rl_udp_peer* peer = NULL;
    rl_udp_config config = {0};
    rl_udp_step_result step = {0};
    uint32_t required = 0;
    config.api_version = RL_API_VERSION;
    config.struct_size = sizeof(config);
    step.api_version = RL_API_VERSION;
    step.struct_size = sizeof(step);
    if (rl_udp_peer_create(NULL, NULL, &peer) != RL_INVALID_ARGUMENT) return __LINE__;
    if (rl_udp_peer_create(&config, NULL, NULL) != RL_INVALID_ARGUMENT) return __LINE__;
    if (rl_udp_peer_destroy(NULL) != RL_INVALID_ARGUMENT) return __LINE__;
    if (rl_udp_peer_step(NULL, 0, &step) != RL_INVALID_ARGUMENT) return __LINE__;
    if (rl_udp_peer_get_correction(NULL, NULL) != RL_INVALID_ARGUMENT) return __LINE__;
    if (rl_udp_peer_copy_report(NULL, NULL, 0, &required) != RL_INVALID_ARGUMENT) return __LINE__;
    if (rl_udp_peer_copy_replay(NULL, NULL, 0, &required) != RL_INVALID_ARGUMENT) return __LINE__;
    if (rl_udp_peer_copy_failure(NULL, NULL, 0, &required) != RL_INVALID_ARGUMENT) return __LINE__;
    return 0;
}

int rl_udp_c11_live_checks(uint32_t relay_port) {
    rl_session_config session_config = {0};
    rl_session* session = NULL;
    rl_udp_peer* peer = NULL;
    rl_udp_config config = {0};
    rl_udp_step_result step = {0};
    char output[4096];
    uint32_t required = 0;
    int result = 0;
    session_config.api_version = RL_API_VERSION;
    session_config.struct_size = sizeof(session_config);
    session_config.max_rollback_frames = 120;
    config.api_version = RL_API_VERSION;
    config.struct_size = sizeof(config);
    config.frame_count = 1;
    config.relay_port = relay_port;
    config.handshake_timeout_ms = 100;
    config.run_timeout_ms = 100;
    step.api_version = RL_API_VERSION;
    step.struct_size = sizeof(step);
    if (rl_session_create(&session_config, &session) != RL_OK) return __LINE__;
    if (rl_udp_peer_create(&config, session, &peer) != RL_OK) result = __LINE__;
    else if (rl_udp_peer_copy_failure(peer, output, sizeof(output), &required) != RL_OK ||
             strstr(output, "\"udp_profile\":\"engine-udp-v1\"") == NULL ||
             strstr(output, "\"listen_port\":0,") != NULL) result = __LINE__;
    else if (rl_udp_peer_step(peer, 0, &step) != RL_OK || step.phase != RL_UDP_HANDSHAKE) result = __LINE__;
    else if (rl_session_destroy(session) != RL_BORROWED) result = __LINE__;
    else if (rl_udp_peer_step(peer, 101, &step) != RL_TIMEOUT || step.phase != RL_UDP_FAILED) result = __LINE__;
    if (peer != NULL && rl_udp_peer_destroy(peer) != RL_OK) result = __LINE__;
    if (rl_session_destroy(session) != RL_OK) result = __LINE__;
    return result;
}
