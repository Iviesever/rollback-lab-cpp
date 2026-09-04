#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <new>
#include <rollback_lab/c_api/rollback_lab_c.h>
#include <rollback_lab/core/error.hpp>
#include <rollback_lab/protocol/codec.hpp>
#include <rollback_lab/transport/udp_socket.hpp>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
thread_local bool fail_next_allocation = false;
thread_local unsigned injected_allocations = 0;
} // namespace
// Fail a throwing production buffer allocation, not arbitrary STL
// bookkeeping. MSVC Debug creates 16-byte iterator proxies in noexcept default
// constructors; failing those deliberately terminates before Core can catch.
// A 32-byte threshold excludes those proxies and reaches Core buffer growth
// (canonical or packet serialization). There is no production fault hook.
void* operator new(std::size_t size) {
    if (fail_next_allocation && size >= 32U) {
        fail_next_allocation = false;
        std::fprintf(stderr, "Injected production buffer allocation size=%zu\n", size);
        std::fflush(stderr);
        ++injected_allocations;
        throw std::bad_alloc{};
    }
    if (auto* p = std::malloc(size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc{};
}
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }

namespace {
void require(bool condition, const char* reason) {
    if (!condition)
        throw std::runtime_error(reason);
}
template <class T> auto initialized() -> T {
    T value{};
    value.api_version = RL_API_VERSION;
    value.struct_size = sizeof(T);
    return value;
}
struct Handles final {
    rl_session* session{};
    rl_udp_peer* peer{};
    ~Handles() {
        fail_next_allocation = false;
        if (peer)
            static_cast<void>(rl_udp_peer_destroy(peer));
        if (session)
            static_cast<void>(rl_session_destroy(session));
    }
};
auto failure_json(rl_udp_peer* peer) -> std::string {
    uint32_t required{};
    require(rl_udp_peer_copy_failure(peer, nullptr, 0, &required) == RL_BUFFER_TOO_SMALL,
            "failure size");
    std::vector<char> buffer(required);
    require(rl_udp_peer_copy_failure(peer, buffer.data(), required, &required) == RL_OK,
            "failure copy");
    return buffer.data();
}
} // namespace

int main() {
    try {
        using namespace rollback_lab;
        auto relay = UdpSocket::bind_loopback(0);
        require(relay.ok(), "bind relay");
        Handles handles;
        auto session_config = initialized<rl_session_config>();
        session_config.max_rollback_frames = 120;
        require(rl_session_create(&session_config, &handles.session) == RL_OK, "create session");
        auto config = initialized<rl_udp_config>();
        config.scenario_seed = 1;
        config.transport_seed = 2;
        config.frame_count = 5;
        config.relay_port = relay.value().local_port();
        config.handshake_timeout_ms = 1000;
        config.run_timeout_ms = 1000;
        require(rl_udp_peer_create(&config, handles.session, &handles.peer) == RL_OK,
                "create peer");
        const auto initial = failure_json(handles.peer);
        constexpr auto port_key = "\"listen_port\":";
        const auto port_at = initial.find(port_key);
        require(port_at != std::string::npos, "bound port metadata");
        const auto local_port = static_cast<uint16_t>(std::stoul(initial.substr(port_at + 14U)));
        auto step = initialized<rl_udp_step_result>();
        require(rl_udp_peer_step(handles.peer, 0, &step) == RL_OK, "send hello");
        auto received = relay.value().receive_for(std::chrono::milliseconds{50});
        require(received.ok() && received.value().has_value(), "receive real hello");
        auto hello = decode_packet(received.value()->bytes);
        require(hello.ok(), "decode hello");
        hello.value().sender = PlayerId::b;
        auto encoded = encode_packet(hello.value());
        require(encoded.ok(), "encode remote hello");
        require(relay.value().send_loopback(local_port, encoded.value()).ok(), "send remote hello");
        require(rl_udp_peer_step(handles.peer, 1, &step) == RL_OK && step.phase == RL_UDP_RUNNING,
                "complete handshake before allocation fault");

        fail_next_allocation = true;
        const auto status = rl_udp_peer_step(handles.peer, 2, &step);
        fail_next_allocation = false;
        require(injected_allocations == 1U, "genuine one-shot allocation fault was not reached");
        require(status == RL_INTERNAL_FAILURE, "exception must map to RL_INTERNAL_FAILURE");
        const auto failure = failure_json(handles.peer);
        std::cout << failure << '\n';
        require(step.phase == RL_UDP_FAILED && !step.finished,
                "exception left copied observation nonterminal");
        require(failure.find("\"phase\":4") != std::string::npos,
                "exception left native phase nonterminal");
        require(failure.find("\"context\":\"udp_internal_exception\"") != std::string::npos,
                "exception omitted native failure context");
        require(failure.find("\"sdk_status\":9") != std::string::npos,
                "failure status differs from ABI result");
        require(failure.find("\"error_code\":" +
                             std::to_string(static_cast<unsigned>(ErrorCode::internal_failure))) !=
                    std::string::npos,
                "native exception must have typed internal failure code");
        auto before = initialized<rl_hash_result>();
        require(rl_session_get_hash(handles.session, &before) == RL_OK,
                "snapshot after contained failure");
        const auto logical_tick = step.logical_tick;
        require(rl_udp_peer_step(handles.peer, 3, &step) == RL_INTERNAL_FAILURE,
                "failed driver retried mutation");
        auto after = initialized<rl_hash_result>();
        require(rl_session_get_hash(handles.session, &after) == RL_OK,
                "snapshot after terminal retry");
        require(before.frame == after.frame && before.state_hash == after.state_hash &&
                    step.phase == RL_UDP_FAILED && step.logical_tick == logical_tick,
                "terminal retry changed state");
        require(failure_json(handles.peer) == failure, "terminal retry changed diagnostic");
        std::cout << "one-shot UDP allocation failure: status, phase, context and terminal retry "
                     "passed\n";
        return 0;
    } catch (const std::exception& error) {
        fail_next_allocation = false;
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
