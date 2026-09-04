#include "test_framework.hpp"
#include <array>
#include <chrono>
#include <filesystem>
#include <rollback_lab/c_api/rollback_lab_c.h>
#include <rollback_lab/protocol/codec.hpp>
#include <rollback_lab/replay/replay.hpp>
#include <rollback_lab/transport/process.hpp>
#include <rollback_lab/transport/udp_socket.hpp>
#include <string>
#include <thread>
#include <vector>

extern "C" int rl_udp_c11_checks(void);
extern "C" int rl_udp_c11_live_checks(uint32_t relay_port);
namespace {
using namespace rollback_lab;
template <class T> auto value() -> T {
    T v{};
    v.api_version = RL_API_VERSION;
    v.struct_size = sizeof(T);
    return v;
}
auto port() -> uint16_t {
    auto s = UdpSocket::bind_loopback(0);
    RL_REQUIRE(s.ok());
    return s.value().local_port();
}
auto config(uint16_t local, uint16_t relay) -> rl_udp_config {
    auto c = value<rl_udp_config>();
    c.scenario_seed = 1;
    c.transport_seed = 2;
    c.frame_count = 240;
    c.listen_port = local;
    c.relay_port = relay;
    c.handshake_timeout_ms = 1000;
    c.run_timeout_ms = 15000;
    return c;
}
struct Peer {
    rl_session* session{};
    rl_udp_peer* driver{};
    ~Peer() {
        if (driver)
            static_cast<void>(rl_udp_peer_destroy(driver));
        if (session)
            static_cast<void>(rl_session_destroy(session));
    }
    void create(uint32_t id, const rl_udp_config& c, uint32_t variant = RL_VARIANT_CANONICAL) {
        auto s = value<rl_session_config>();
        s.local_peer = id;
        s.max_rollback_frames = 120;
        s.simulation_variant = variant;
        RL_REQUIRE(rl_session_create(&s, &session) == RL_OK);
        RL_REQUIRE(rl_udp_peer_create(&c, session, &driver) == RL_OK);
    }
};
using CopyJson = rl_status (*)(rl_udp_peer*, char*, uint32_t, uint32_t*);
auto json(rl_udp_peer* p, CopyJson copy) -> std::string {
    uint32_t n{};
    RL_REQUIRE(copy(p, nullptr, 0, &n) == RL_BUFFER_TOO_SMALL);
    RL_REQUIRE(n > 1);
    std::vector<char> bytes(n, 'x');
    auto saved = bytes;
    RL_CHECK(copy(p, bytes.data(), n - 1, &n) == RL_BUFFER_TOO_SMALL);
    RL_CHECK(bytes == saved);
    RL_REQUIRE(copy(p, bytes.data(), n, &n) == RL_OK);
    RL_CHECK(bytes.back() == '\0');
    return bytes.data();
}
auto replay(rl_udp_peer* p) -> std::vector<std::byte> {
    uint32_t n{};
    RL_REQUIRE(rl_udp_peer_copy_replay(p, nullptr, 0, &n) == RL_BUFFER_TOO_SMALL);
    std::vector<std::byte> b(n);
    RL_REQUIRE(rl_udp_peer_copy_replay(p, reinterpret_cast<uint8_t*>(b.data()), n, &n) == RL_OK);
    return b;
}
struct Relay {
    uint16_t relay_port{port()}, a_port{port()}, b_port{port()};
    std::filesystem::path ready;
    std::optional<ChildProcess> child;
    Relay() {
        RL_REQUIRE(relay_port != a_port && relay_port != b_port && a_port != b_port);
        ready = std::filesystem::temp_directory_path() /
                ("rl-c-udp-" + std::to_string(relay_port) + ".ready");
        std::error_code error;
        std::filesystem::remove(ready, error);
#if defined(_WIN32)
        const auto executable = std::filesystem::current_path() / "rollback_lab.exe";
#else
        const auto executable = std::filesystem::current_path() / "rollback_lab";
#endif
        auto created = ChildProcess::spawn(executable,
                                           {"relay", "--relay-port", std::to_string(relay_port),
                                            "--peer-a-port", std::to_string(a_port),
                                            "--peer-b-port", std::to_string(b_port), "--ready",
                                            ready.string(), "--max-ms", "10000"});
        RL_REQUIRE(created.ok());
        child.emplace(std::move(created.value()));
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
        while (!std::filesystem::exists(ready) && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        RL_REQUIRE(std::filesystem::exists(ready));
    }
    ~Relay() {
        child.reset();
        std::error_code e;
        std::filesystem::remove(ready, e);
    }
};
struct PairResult {
    rl_status a{RL_OK}, b{RL_OK};
    bool correction{};
    rl_udp_step_result sa{value<rl_udp_step_result>()}, sb{value<rl_udp_step_result>()};
};
auto pump(Peer& a, Peer& b) -> PairResult {
    PairResult r;
    for (uint64_t tick = 0; tick < 1600 && (!r.sa.finished || !r.sb.finished); ++tick) {
        if (r.a == RL_OK)
            r.a = rl_udp_peer_step(a.driver, tick * 2, &r.sa);
        if (r.b == RL_OK)
            r.b = rl_udp_peer_step(b.driver, tick * 2, &r.sb);
        for (auto* driver : {a.driver, b.driver}) {
            auto correction = value<rl_live_correction>();
            RL_REQUIRE(rl_udp_peer_get_correction(driver, &correction) == RL_OK);
            if (correction.performed) {
                r.correction = true;
                RL_CHECK(correction.resimulated_frames > 0);
                RL_CHECK(correction.before.frame == correction.after.frame);
            }
        }
        if (r.a != RL_OK || r.b != RL_OK)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    return r;
}
} // namespace

RL_TEST(c_udp_c11_surface_and_creation_contract) {
    RL_CHECK(rl_udp_c11_checks() == 0);
    auto relay = UdpSocket::bind_loopback(0);
    RL_REQUIRE(relay.ok());
    RL_CHECK(rl_udp_c11_live_checks(relay.value().local_port()) == 0);
    auto v = value<rl_version_info>();
    RL_REQUIRE(rl_get_version(&v) == RL_OK);
    RL_CHECK((v.capabilities & RL_CAP_UDP) != 0);
    auto c = config(port(), port());
    Peer a;
    auto s = value<rl_session_config>();
    s.max_rollback_frames = 120;
    RL_REQUIRE(rl_session_create(&s, &a.session) == RL_OK);
    auto bad = c;
    bad.api_version = 99;
    RL_CHECK(rl_udp_peer_create(&bad, a.session, &a.driver) == RL_ABI_VERSION);
    RL_CHECK(!a.driver);
    bad = c;
    --bad.struct_size;
    RL_CHECK(rl_udp_peer_create(&bad, a.session, &a.driver) == RL_STRUCT_SIZE);
    for (auto frames : {0U, 241U}) {
        bad = c;
        bad.frame_count = frames;
        RL_CHECK(rl_udp_peer_create(&bad, a.session, &a.driver) == RL_INVALID_ARGUMENT);
    }
    bad = c;
    bad.reserved[1] = 1;
    RL_CHECK(rl_udp_peer_create(&bad, a.session, &a.driver) == RL_INVALID_ARGUMENT);
    RL_REQUIRE(rl_udp_peer_create(&c, a.session, &a.driver) == RL_OK);
    RL_CHECK(rl_session_destroy(a.session) == RL_BORROWED);
    auto input = value<rl_input_frame>();
    auto advance = value<rl_advance_result>();
    RL_CHECK(rl_session_advance(a.session, &input, &advance) == RL_BORROWED);
    RL_CHECK(rl_session_ingest_remote(a.session, &input) == RL_BORROWED);
    rl_udp_peer* other{};
    RL_CHECK(rl_udp_peer_create(&c, a.session, &other) == RL_BORROWED);
    auto step = value<rl_udp_step_result>();
    step.struct_size = 1;
    RL_CHECK(rl_udp_peer_step(a.driver, 0, &step) == RL_STRUCT_SIZE);
    step = value<rl_udp_step_result>();
    rl_status wrong{};
    std::array<rl_status, 4> wrong_calls{};
    std::thread t([&] {
        wrong = rl_udp_peer_step(a.driver, 0, &step);
        auto correction = value<rl_live_correction>();
        uint32_t needed{};
        wrong_calls = {rl_udp_peer_destroy(a.driver),
                       rl_udp_peer_get_correction(a.driver, &correction),
                       rl_udp_peer_copy_failure(a.driver, nullptr, 0, &needed),
                       rl_udp_peer_copy_report(a.driver, nullptr, 0, &needed)};
    });
    t.join();
    RL_CHECK(wrong == RL_WRONG_THREAD);
    for (auto status : wrong_calls)
        RL_CHECK(status == RL_WRONG_THREAD);
    uint32_t n{};
    RL_CHECK(rl_udp_peer_copy_report(a.driver, nullptr, 0, &n) == RL_INVALID_FRAME);
    RL_CHECK(json(a.driver, rl_udp_peer_copy_failure).find("engine-udp-v1") != std::string::npos);
}
RL_TEST(c_udp_real_relay_prediction_correction_replay_terminal) {
    Relay relay;
    Peer a, b;
    auto ca = config(relay.a_port, relay.relay_port), cb = config(relay.b_port, relay.relay_port);
    a.create(RL_PEER_A, ca);
    b.create(RL_PEER_B, cb);
    RL_CHECK(a.session != b.session);
    const auto r = pump(a, b);
    RL_REQUIRE(r.a == RL_OK && r.b == RL_OK);
    RL_REQUIRE(r.sa.finished && r.sb.finished);
    RL_CHECK(r.correction);
    auto ma = value<rl_metrics>(), mb = value<rl_metrics>();
    RL_REQUIRE(rl_session_get_metrics(a.session, &ma) == RL_OK);
    RL_REQUIRE(rl_session_get_metrics(b.session, &mb) == RL_OK);
    RL_CHECK(ma.confirmed_frame == 240 && mb.confirmed_frame == 240);
    RL_CHECK(ma.predicted_input_count + mb.predicted_input_count > 0);
    RL_CHECK(ma.rollback_count + mb.rollback_count > 0);
    const auto ba = replay(a.driver), bb = replay(b.driver);
    RL_CHECK(ba == bb);
    const auto decoded = decode_replay(ba);
    RL_REQUIRE(decoded.ok());
    RL_CHECK(verify_replay(decoded.value()).ok());
    auto h = value<rl_hash_result>();
    RL_REQUIRE(rl_session_get_hash(a.session, &h) == RL_OK);
    RL_CHECK(h.state_hash == decoded.value().expected_final_hash);
    RL_CHECK(json(a.driver, rl_udp_peer_copy_report).find("\"success\":true") != std::string::npos);
    auto step = value<rl_udp_step_result>();
    RL_REQUIRE(rl_udp_peer_step(a.driver, 99999, &step) == RL_OK);
    RL_CHECK(step.finished && step.logical_tick == r.sa.logical_tick);
}
RL_TEST(c_udp_missing_peer_timeout_and_failure_is_terminal) {
    auto relay = UdpSocket::bind_loopback(0);
    RL_REQUIRE(relay.ok());
    Peer a;
    auto c = config(port(), relay.value().local_port());
    a.create(0, c);
    auto step = value<rl_udp_step_result>();
    RL_REQUIRE(rl_udp_peer_step(a.driver, 0, &step) == RL_OK);
    RL_CHECK(rl_udp_peer_step(a.driver, 1001, &step) == RL_TIMEOUT);
    RL_CHECK(rl_udp_peer_step(a.driver, 1002, &step) == RL_TIMEOUT);
    auto snap = value<rl_world_snapshot>();
    RL_REQUIRE(rl_session_get_snapshot(a.session, &snap) == RL_OK);
    RL_CHECK(snap.frame == 0);
    RL_CHECK(json(a.driver, rl_udp_peer_copy_failure).find("peer_handshake") != std::string::npos);
}
RL_TEST(c_udp_network_version_and_aggregate_abi_profile_fail_closed) {
    for (uint32_t kind = 0; kind < 7; ++kind) {
        Relay relay;
        Peer a, b;
        auto ca = config(relay.a_port, relay.relay_port),
             cb = config(relay.b_port, relay.relay_port);
        if (kind == 0)
            cb.advertised_protocol_version = 99;
        else if (kind == 1)
            cb.advertised_simulation_version = 99;
        else if (kind == 2)
            cb.advertised_abi_profile_version = 99;
        else if (kind == 3)
            cb.scenario_seed = 99;
        else if (kind == 5)
            cb.transport_seed = 99;
        else if (kind == 6)
            cb.frame_count = 120;
        a.create(0, ca);
        b.create(kind == 4 ? 0U : 1U, cb);
        const auto r = pump(a, b);
        const auto expected = kind < 2
                                  ? RL_NETWORK_VERSION
                                  : ((kind == 3 || kind == 4) ? RL_PACKET : RL_HANDSHAKE_PROFILE);
        RL_CHECK(r.a == expected || r.b == expected);
    }
}
RL_TEST(c_udp_watchdog_and_confirmed_desync) {
    for (uint32_t kind = 0; kind < 3; ++kind) {
        Relay relay;
        Peer a, b;
        auto ca = config(relay.a_port, relay.relay_port),
             cb = config(relay.b_port, relay.relay_port);
        if (kind == 0) {
            ca.run_timeout_ms = 1;
            cb.run_timeout_ms = 1;
        }
        a.create(0, ca, kind == 2 ? RL_VARIANT_DAMAGE_BIAS : RL_VARIANT_CANONICAL);
        b.create(1, cb, kind != 0 ? RL_VARIANT_DAMAGE_BIAS : RL_VARIANT_CANONICAL);
        const auto r = pump(a, b);
        const auto expected = kind == 0 ? RL_TIMEOUT : (kind == 1 ? RL_DESYNC : RL_REPLAY_MISMATCH);
        RL_CHECK(r.a == expected || r.b == expected);
        const auto failure = json(r.a != RL_OK ? a.driver : b.driver, rl_udp_peer_copy_failure);
        RL_CHECK(failure.find(kind == 0
                                  ? "udp_peer_run"
                                  : (kind == 1 ? "earliest_divergent_frame"
                                               : "udp_canonical_replay")) != std::string::npos);
    }
}
RL_TEST(c_udp_invalid_future_input_and_backwards_clock_reject_without_advancing) {
    auto relay = UdpSocket::bind_loopback(0);
    RL_REQUIRE(relay.ok());
    Peer a;
    auto c = config(port(), relay.value().local_port());
    a.create(0, c);
    auto step = value<rl_udp_step_result>();
    RL_REQUIRE(rl_udp_peer_step(a.driver, 1, &step) == RL_OK);
    RL_CHECK(rl_udp_peer_step(a.driver, 0, &step) == RL_INVALID_ARGUMENT);
    auto hello = relay.value().receive_for(std::chrono::milliseconds{50});
    RL_REQUIRE(hello.ok() && hello.value());
    auto packet = decode_packet(hello.value()->bytes);
    RL_REQUIRE(packet.ok());
    packet.value().sender = PlayerId::b;
    auto bytes = encode_packet(packet.value());
    RL_REQUIRE(bytes.ok());
    RL_REQUIRE(
        relay.value().send_loopback(static_cast<uint16_t>(c.listen_port), bytes.value()).ok());
    RL_REQUIRE(rl_udp_peer_step(a.driver, 2, &step) == RL_OK);
    RL_CHECK(step.handshake_complete);
    Packet bad{};
    bad.type = PacketType::input;
    bad.sender = PlayerId::b;
    bad.scenario_id = c.scenario_seed;
    bad.sequence = 3;
    bad.inputs.push_back(InputFrame{FrameNumber{240}, PlayerId::b, 1, 0});
    bytes = encode_packet(bad);
    RL_REQUIRE(bytes.ok());
    RL_REQUIRE(
        relay.value().send_loopback(static_cast<uint16_t>(c.listen_port), bytes.value()).ok());
    RL_CHECK(rl_udp_peer_step(a.driver, 3, &step) == RL_INVALID_FRAME);
    auto snapshot = value<rl_world_snapshot>();
    RL_REQUIRE(rl_session_get_snapshot(a.session, &snapshot) == RL_OK);
    RL_CHECK(snapshot.frame == 0);
    RL_CHECK(json(a.driver, rl_udp_peer_copy_failure).find("udp_input_target") !=
             std::string::npos);
}
