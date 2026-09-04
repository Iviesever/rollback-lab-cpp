#include "test_framework.hpp"
#include "abi_layout_checks.h"

#include <rollback_lab/c_api/rollback_lab_c.h>
#include <rollback_lab/netcode/session.hpp>
#include <rollback_lab/simulation/scripted_input.hpp>

#include <array>
#include <cstddef>
#include <thread>
#include <vector>

namespace {
template <class T> auto initialized() -> T {
    T value{};
    value.api_version = RL_API_VERSION;
    value.struct_size = sizeof(T);
    return value;
}
auto configuration(std::uint32_t peer = RL_PEER_A) -> rl_session_config {
    auto value = initialized<rl_session_config>();
    value.local_peer = peer;
    value.max_rollback_frames = 120;
    return value;
}
struct Session {
    rl_session* handle{};
    explicit Session(std::uint32_t peer = RL_PEER_A) {
        const auto config = configuration(peer);
        RL_REQUIRE(rl_session_create(&config, &handle) == RL_OK);
    }
    ~Session() { if (handle) static_cast<void>(rl_session_destroy(handle)); }
};
auto input(std::uint32_t frame, std::uint32_t peer, std::uint8_t buttons = 0)
    -> rl_input_frame {
    auto value = initialized<rl_input_frame>();
    value.frame = frame;
    value.sequence = frame;
    value.peer = peer;
    value.buttons = buttons;
    return value;
}
auto snapshot(rl_session* handle) -> rl_world_snapshot {
    auto result = initialized<rl_world_snapshot>();
    RL_REQUIRE(rl_session_get_snapshot(handle, &result) == RL_OK);
    return result;
}
}

RL_TEST(c_abi_rejects_null_version_size_config_without_allocating) {
    rl_session* handle{};
    auto config = configuration();
    RL_CHECK(rl_session_create(nullptr, &handle) == RL_INVALID_ARGUMENT);
    RL_CHECK(rl_session_create(&config, nullptr) == RL_INVALID_ARGUMENT);
    RL_CHECK(rl_session_destroy(nullptr) == RL_INVALID_ARGUMENT);
    config.api_version = 2;
    RL_CHECK(rl_session_create(&config, &handle) == RL_ABI_VERSION);
    config = configuration(); config.struct_size -= 1;
    RL_CHECK(rl_session_create(&config, &handle) == RL_STRUCT_SIZE);
    config = configuration(); config.struct_size += 1;
    RL_CHECK(rl_session_create(&config, &handle) == RL_STRUCT_SIZE);
    config = configuration(); config.local_peer = 2;
    RL_CHECK(rl_session_create(&config, &handle) == RL_INVALID_PEER);
    config = configuration(); config.max_rollback_frames = 121;
    RL_CHECK(rl_session_create(&config, &handle) == RL_INVALID_ARGUMENT);
    config.max_rollback_frames = 0;
    RL_CHECK(rl_session_create(&config, &handle) == RL_INVALID_ARGUMENT);
    config = configuration(); config.simulation_variant = 2;
    RL_CHECK(rl_session_create(&config, &handle) == RL_INVALID_ARGUMENT);
    config = configuration(); config.reserved = 1;
    RL_CHECK(rl_session_create(&config, &handle) == RL_INVALID_ARGUMENT);
    RL_CHECK(handle == nullptr);
}

RL_TEST(c_abi_version_query_validates_header_and_preserves_output_on_failure) {
    RL_CHECK(rl_get_version(nullptr) == RL_INVALID_ARGUMENT);
    auto version = initialized<rl_version_info>();
    version.api_version = 2; version.sdk_minor = 55;
    RL_CHECK(rl_get_version(&version) == RL_ABI_VERSION);
    RL_CHECK(version.sdk_minor == 55);
    version.api_version = 1; version.struct_size -= 1;
    RL_CHECK(rl_get_version(&version) == RL_STRUCT_SIZE);
    version = initialized<rl_version_info>();
    RL_REQUIRE(rl_get_version(&version) == RL_OK);
    RL_CHECK(version.sdk_major == 0 && version.sdk_minor == 2 && version.sdk_patch == 0);
    RL_CHECK(version.simulation_version == 1 && version.protocol_version == 1 && version.replay_version == 1);
    RL_CHECK((version.capabilities & (RL_CAP_SESSION | RL_CAP_LIVE | RL_CAP_CANONICAL_BYTES)) ==
             (RL_CAP_SESSION | RL_CAP_LIVE | RL_CAP_CANONICAL_BYTES));
    RL_CHECK(version.source_git_sha[40] == '\0');
    for (auto byte : version.reserved) RL_CHECK(byte == 0);
}

RL_TEST(c_abi_rejects_bad_inputs_and_outputs_before_mutation) {
    Session session;
    const auto before = snapshot(session.handle);
    auto local = input(0, RL_PEER_A);
    auto advance = initialized<rl_advance_result>();
    RL_CHECK(rl_session_advance(nullptr, &local, &advance) == RL_INVALID_ARGUMENT);
    RL_CHECK(rl_session_advance(session.handle, nullptr, &advance) == RL_INVALID_ARGUMENT);
    RL_CHECK(rl_session_advance(session.handle, &local, nullptr) == RL_INVALID_ARGUMENT);
    advance.api_version = 2;
    RL_CHECK(rl_session_advance(session.handle, &local, &advance) == RL_ABI_VERSION);
    advance = initialized<rl_advance_result>(); advance.struct_size = 8;
    RL_CHECK(rl_session_advance(session.handle, &local, &advance) == RL_STRUCT_SIZE);
    advance = initialized<rl_advance_result>(); local.peer = RL_PEER_B;
    RL_CHECK(rl_session_advance(session.handle, &local, &advance) == RL_INVALID_PEER);
    local = input(1, RL_PEER_A);
    RL_CHECK(rl_session_advance(session.handle, &local, &advance) == RL_INVALID_FRAME);
    local = input(0, RL_PEER_A, 0x80);
    RL_CHECK(rl_session_advance(session.handle, &local, &advance) == RL_INVALID_ARGUMENT);
    local = input(0, RL_PEER_A); local.reserved[1] = 1;
    RL_CHECK(rl_session_advance(session.handle, &local, &advance) == RL_INVALID_ARGUMENT);
    auto remote = input(0, RL_PEER_A);
    RL_CHECK(rl_session_ingest_remote(session.handle, &remote) == RL_INVALID_PEER);
    remote = input(256, RL_PEER_B);
    RL_CHECK(rl_session_ingest_remote(session.handle, &remote) == RL_INVALID_FRAME);
    RL_CHECK(snapshot(session.handle).state_hash == before.state_hash);
    auto bad = initialized<rl_world_snapshot>(); bad.api_version = 99; bad.frame = 987;
    RL_CHECK(rl_session_get_snapshot(session.handle, &bad) == RL_ABI_VERSION);
    RL_CHECK(bad.frame == 987);
}

RL_TEST(c_abi_two_sessions_are_isolated_and_thread_affine) {
    Session a; Session b(RL_PEER_B);
    const auto original = snapshot(b.handle);
    auto local = input(0, RL_PEER_A, RL_BUTTON_RIGHT);
    auto advanced = initialized<rl_advance_result>();
    RL_CHECK(rl_session_advance(a.handle, &local, &advanced) == RL_OK);
    RL_CHECK(snapshot(a.handle).state_hash != original.state_hash);
    RL_CHECK(snapshot(b.handle).state_hash == original.state_hash);
    rl_status query_status = RL_OK;
    rl_status destroy_status = RL_OK;
    std::thread worker([&] {
        auto result = initialized<rl_world_snapshot>();
        query_status = rl_session_get_snapshot(a.handle, &result);
        destroy_status = rl_session_destroy(a.handle);
    });
    worker.join();
    RL_CHECK(query_status == RL_WRONG_THREAD);
    RL_CHECK(destroy_status == RL_WRONG_THREAD);
    RL_CHECK(snapshot(a.handle).frame == 1);
}

RL_TEST(c_abi_window_stale_and_two_phase_canonical_buffer) {
    Session session;
    auto result = initialized<rl_advance_result>();
    for (std::uint32_t frame = 0; frame < 121; ++frame) {
        auto local = input(frame, RL_PEER_A);
        RL_REQUIRE(rl_session_advance(session.handle, &local, &result) == RL_OK);
    }
    auto remote = input(0, RL_PEER_B);
    RL_CHECK(rl_session_ingest_remote(session.handle, &remote) == RL_ROLLBACK_WINDOW);
    remote.frame = 1;
    RL_CHECK(rl_session_ingest_remote(session.handle, &remote) == RL_OK);
    auto hash = initialized<rl_hash_result>();
    RL_CHECK(rl_session_hash_at(session.handle, 121, &hash) == RL_STALE_FRAME);
    std::uint32_t required = 0;
    RL_CHECK(rl_session_serialize_state(session.handle, nullptr, 0, &required) == RL_BUFFER_TOO_SMALL);
    RL_REQUIRE(required > 0);
    std::vector<std::uint8_t> buffer(required, 0x55);
    RL_CHECK(rl_session_serialize_state(session.handle, buffer.data(), required - 1, &required) == RL_BUFFER_TOO_SMALL);
    RL_CHECK(buffer[0] == 0x55);
    RL_CHECK(rl_session_serialize_state(session.handle, buffer.data(), required, &required) == RL_OK);
    RL_CHECK(buffer[0] == 1);
    RL_CHECK(rl_session_serialize_state(session.handle, nullptr, required, &required) == RL_INVALID_ARGUMENT);
}

RL_TEST(c_abi_future_input_cannot_replace_last_known_prediction_history) {
    Session session;
    auto remote = input(0, RL_PEER_B, RL_BUTTON_RIGHT);
    auto local = input(0, RL_PEER_A);
    auto advanced = initialized<rl_advance_result>();
    RL_REQUIRE(rl_session_ingest_remote(session.handle, &remote) == RL_OK);
    RL_REQUIRE(rl_session_advance(session.handle, &local, &advanced) == RL_OK);
    remote.frame = 256U;
    remote.buttons = RL_BUTTON_LEFT;
    RL_CHECK(rl_session_ingest_remote(session.handle, &remote) == RL_INVALID_FRAME);
    remote.frame = 2U;
    RL_CHECK(rl_session_ingest_remote(session.handle, &remote) == RL_INVALID_FRAME);
    local.frame = 1U;
    RL_REQUIRE(rl_session_advance(session.handle, &local, &advanced) == RL_OK);
    const auto after = snapshot(session.handle);
    RL_CHECK(after.players[1].x == rollback_lab::kPlayerBSpawnX + 2 * rollback_lab::kPlayerSpeed);
}

RL_TEST(c_abi_direct_session_parity_across_128_delayed_input_scenarios) {
    using namespace rollback_lab;
    for (std::uint64_t seed = 0; seed < 128; ++seed) {
        Session session;
        RollbackSession native(SessionConfig{});
        std::array<InputPair, 160> scripted{};
        for (std::uint32_t frame = 0; frame < 160; ++frame) {
            scripted[frame] = {scripted_input(seed, {frame}, PlayerId::a),
                               scripted_input(seed, {frame}, PlayerId::b)};
        }
        const std::uint32_t delay = static_cast<std::uint32_t>(seed % 24 + 1);
        auto result = initialized<rl_advance_result>();
        auto correction = initialized<rl_correction_result>();
        for (std::uint32_t frame = 0; frame < 160; ++frame) {
            if (frame >= delay) {
                const auto& value = scripted[frame - delay].b;
                auto remote = input(value.frame.value, RL_PEER_B, value.buttons);
                remote.sequence = value.sequence;
                RL_REQUIRE(rl_session_ingest_remote(session.handle, &remote) == RL_OK);
                RL_REQUIRE(native.ingest_remote(value).ok());
            }
            const auto corrected = native.flush_corrections();
            RL_REQUIRE(corrected.ok());
            RL_REQUIRE(rl_session_flush_corrections(session.handle, &correction) == RL_OK);
            RL_CHECK(correction.performed == static_cast<std::uint32_t>(corrected.value().performed));
            RL_CHECK(correction.rollback_from == corrected.value().rollback_from.value);
            RL_CHECK(correction.resimulated_frames == corrected.value().resimulated_frames);
            const auto& value = scripted[frame].a;
            auto local = input(frame, RL_PEER_A, value.buttons);
            local.sequence = value.sequence;
            const auto advanced = native.advance(value);
            RL_REQUIRE(advanced.ok());
            RL_REQUIRE(rl_session_advance(session.handle, &local, &result) == RL_OK);
            RL_CHECK(result.state_hash == advanced.value().state_hash);
            RL_CHECK(result.predicted_remote == static_cast<std::uint32_t>(advanced.value().predicted_remote));
        }
        for (std::uint32_t frame = 160 - delay; frame < 160; ++frame) {
            const auto& value = scripted[frame].b;
            auto remote = input(frame, RL_PEER_B, value.buttons); remote.sequence = value.sequence;
            RL_REQUIRE(rl_session_ingest_remote(session.handle, &remote) == RL_OK);
            RL_REQUIRE(native.ingest_remote(value).ok());
        }
        RL_REQUIRE(rl_session_flush_corrections(session.handle, &correction) == RL_OK);
        RL_REQUIRE(native.flush_corrections().ok());
        const auto report = native.report();
        const auto copied = snapshot(session.handle);
        RL_CHECK(copied.state_hash == report.final_hash);
        RL_CHECK(copied.players[0].x == report.state.players[0].x);
        RL_CHECK(copied.players[1].hp == report.state.players[1].hp);
        auto metrics = initialized<rl_metrics>();
        RL_REQUIRE(rl_session_get_metrics(session.handle, &metrics) == RL_OK);
        RL_CHECK(metrics.rollback_count == report.metrics.rollback_count);
        RL_CHECK(metrics.total_resimulated_frames == report.metrics.total_resimulated_frames);
        RL_CHECK(metrics.confirmed_frame == 160);
        auto confirmed = initialized<rl_frame_result>();
        RL_REQUIRE(rl_session_get_confirmed_frame(session.handle, &confirmed) == RL_OK);
        RL_CHECK(confirmed.frame == 160);
        auto hash = initialized<rl_hash_result>();
        RL_REQUIRE(rl_session_get_hash(session.handle, &hash) == RL_OK);
        RL_CHECK(hash.state_hash == report.final_hash);
        RL_REQUIRE(rl_session_hash_at(session.handle, 160, &hash) == RL_OK);
        RL_CHECK(hash.state_hash == report.final_hash);
        auto bytes = serialize_canonical(report.state);
        std::vector<std::uint8_t> copy(bytes.size());
        std::uint32_t count{};
        RL_REQUIRE(rl_session_serialize_state(session.handle, copy.data(), static_cast<std::uint32_t>(copy.size()), &count) == RL_OK);
        RL_CHECK(count == bytes.size());
        for (std::size_t i = 0; i < copy.size(); ++i) RL_CHECK(copy[i] == std::to_integer<std::uint8_t>(bytes[i]));
    }
}
