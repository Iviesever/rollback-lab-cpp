#pragma once

#include <rollback_lab/c_api/rollback_lab_c.h>
#include <rollback_lab/netcode/session.hpp>

#include <thread>
#include <utility>

struct rl_session final {
    rollback_lab::RollbackSession native;
    const std::thread::id owner{std::this_thread::get_id()};
    const std::uint32_t local_peer;
    bool borrowed{};
    bool touched{};

    explicit rl_session(rollback_lab::SessionConfig config)
        : native(config), local_peer(static_cast<std::uint32_t>(config.local_peer)) {}
};

namespace rl_detail {
template <class Function> auto boundary(Function&& function) noexcept -> rl_status {
    try { return std::forward<Function>(function)(); }
    catch (...) { return RL_INTERNAL_FAILURE; }
}
template <class T> auto validate(const T* value) noexcept -> rl_status {
    if (!value) return RL_INVALID_ARGUMENT;
    if (value->api_version != RL_API_VERSION) return RL_ABI_VERSION;
    if (value->struct_size != sizeof(T)) return RL_STRUCT_SIZE;
    return RL_OK;
}
template <class T> auto initialized() noexcept -> T {
    T value{};
    value.api_version = RL_API_VERSION;
    value.struct_size = sizeof(T);
    return value;
}
inline auto check_session(const rl_session* session, bool mutation = false) noexcept -> rl_status {
    if (!session) return RL_INVALID_ARGUMENT;
    if (session->owner != std::this_thread::get_id()) return RL_WRONG_THREAD;
    if (mutation && session->borrowed) return RL_BORROWED;
    return RL_OK;
}
auto to_status(rollback_lab::ErrorCode code) noexcept -> rl_status;
void copy_snapshot(const rollback_lab::WorldState& world, rl_world_snapshot& output);
inline auto input(const rl_input_frame& value) noexcept -> rollback_lab::InputFrame {
    return {{value.frame}, static_cast<rollback_lab::PlayerId>(value.peer), value.sequence, value.buttons};
}
inline auto validate_input(const rl_input_frame* value) noexcept -> rl_status {
    const auto status = validate(value);
    if (status != RL_OK) return status;
    if (value->peer > RL_PEER_B) return RL_INVALID_PEER;
    if ((value->buttons & UINT8_C(0xe0)) != 0 || value->reserved[0] ||
        value->reserved[1] || value->reserved[2]) return RL_INVALID_ARGUMENT;
    return RL_OK;
}
} // namespace rl_detail
