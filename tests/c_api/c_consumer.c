#include <rollback_lab/c_api/rollback_lab_c.h>
#include "abi_layout_checks.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

_Static_assert(sizeof(rl_status) == 4, "fixed status width");
_Static_assert(sizeof(rl_session_config) == 24, "config layout");
_Static_assert(sizeof(rl_input_frame) == 24, "input layout");
_Static_assert(sizeof(rl_player_snapshot) == 32, "player layout");
_Static_assert(sizeof(rl_projectile_snapshot) == 32, "projectile layout");
_Static_assert(sizeof(rl_world_snapshot) == 2144, "world layout");
_Static_assert(offsetof(rl_world_snapshot, players) == 32, "players offset");
_Static_assert(offsetof(rl_world_snapshot, projectiles) == 96, "projectiles offset");
_Static_assert(offsetof(rl_world_snapshot, state_hash) == 8, "hash alignment");
_Static_assert(sizeof(rl_advance_result) == 24, "advance layout");
_Static_assert(sizeof(rl_correction_result) == 24, "correction layout");
_Static_assert(sizeof(rl_metrics) == 48, "metrics layout");
_Static_assert(sizeof(rl_frame_result) == 16, "frame layout");
_Static_assert(sizeof(rl_hash_result) == 24, "hash result layout");
_Static_assert(sizeof(rl_version_info) == 88, "version layout");

int main(int argc, char** argv) {
    rl_version_info version = {0};
    rl_session_config config = {0};
    rl_session* session = NULL;
    rl_world_snapshot snapshot = {0};
    rl_input_frame local = {0};
    rl_advance_result result = {0};
    version.api_version = RL_API_VERSION;
    version.struct_size = sizeof(version);
    if (rl_get_version(&version) != RL_OK || version.sdk_minor != 2) return 1;
    if (argc > 1 && strcmp(version.source_git_sha, argv[1]) != 0) return 8;
    config.api_version = RL_API_VERSION;
    config.struct_size = sizeof(config);
    config.local_peer = RL_PEER_A;
    config.max_rollback_frames = 120;
    if (rl_session_create(&config, &session) != RL_OK || session == NULL) return 2;
    snapshot.api_version = RL_API_VERSION;
    snapshot.struct_size = sizeof(snapshot);
    if (rl_session_get_snapshot(session, &snapshot) != RL_OK) return 3;
    if (snapshot.state_hash != UINT64_C(479350575277098922)) return 4;
    local.api_version = RL_API_VERSION;
    local.struct_size = sizeof(local);
    local.buttons = RL_BUTTON_RIGHT;
    result.api_version = RL_API_VERSION;
    result.struct_size = sizeof(result);
    if (rl_session_advance(session, &local, &result) != RL_OK) return 5;
    if (result.simulated_frame != 0 || result.predicted_remote != 1) return 6;
    if (rl_session_destroy(session) != RL_OK) return 7;
    puts("C11 ABI consumer passed");
    return 0;
}
