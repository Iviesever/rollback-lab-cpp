#include <rollback_lab/c_api/rollback_lab_c.h>
#include <rollback_lab/netcode/session.hpp>

#include <iostream>
#include <cstring>

int main(int argc, char** argv) {
    rl_version_info version{};
    version.api_version = RL_API_VERSION;
    version.struct_size = sizeof(version);
    if (rl_get_version(&version) != RL_OK) return 5;
    if (argc > 1 && std::strcmp(version.source_git_sha, argv[1]) != 0) return 6;
    const rl_session_config config{RL_API_VERSION, sizeof(rl_session_config), RL_PEER_A, 120, 0, 0};
    rl_session* session{};
    if (rl_session_create(&config, &session) != RL_OK) return 1;
    rollback_lab::RollbackSession core(rollback_lab::SessionConfig{});
    rl_world_snapshot snapshot{};
    snapshot.api_version = RL_API_VERSION;
    snapshot.struct_size = sizeof(snapshot);
    if (rl_session_get_snapshot(session, &snapshot) != RL_OK) return 2;
    if (snapshot.state_hash != core.report().final_hash) return 3;
    if (rl_session_destroy(session) != RL_OK) return 4;
    std::cout << "C++ installed ABI/core consumer passed\n";
    return 0;
}
