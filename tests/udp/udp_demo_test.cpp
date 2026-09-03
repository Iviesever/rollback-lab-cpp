#include "test_framework.hpp"

#include <rollback_lab/transport/udp_socket.hpp>
#include <rollback_lab/transport/process.hpp>
#include <rollback_lab/udp/demo.hpp>

#include <filesystem>

namespace {

using namespace rollback_lab;

auto executable_path() -> std::filesystem::path {
#if defined(_WIN32)
    return std::filesystem::current_path() / "rollback_lab.exe";
#else
    return std::filesystem::current_path() / "rollback_lab";
#endif
}

auto temp_output(const char* name) -> std::filesystem::path {
    return std::filesystem::temp_directory_path() / name;
}

void clean(const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::remove_all(path, error);
}

}  // namespace

RL_TEST(udp_demo_uses_relay_and_two_independent_peer_processes) {
    const auto output = temp_output("rollback_lab_udp_success");
    clean(output);
    UdpDemoConfig config{};
    config.executable_path = executable_path();
    config.output_directory = output;
    config.frame_count = 90U;
    config.watchdog_milliseconds = 4'000U;
    const auto result = run_udp_demo(config);
    RL_REQUIRE(result.ok());
    RL_CHECK(result.value().relay_pid != 0U);
    RL_CHECK(result.value().peer_a_pid != 0U);
    RL_CHECK(result.value().peer_b_pid != 0U);
    RL_CHECK(result.value().peer_a_pid != result.value().peer_b_pid);
    RL_CHECK(result.value().relay_port != result.value().peer_a_port);
    RL_CHECK(result.value().relay_port != result.value().peer_b_port);
    RL_CHECK(result.value().peer_a_port != result.value().peer_b_port);
    RL_CHECK(result.value().confirmed_frame == FrameNumber{90U});
    RL_CHECK(result.value().final_hash_a == result.value().final_hash_b);
    RL_CHECK(result.value().replay_verified);
    RL_CHECK(result.value().relay_exit_code == 0);
    RL_CHECK(result.value().peer_a_exit_code == 0);
    RL_CHECK(result.value().peer_b_exit_code == 0);
    RL_CHECK(std::filesystem::is_regular_file(output / "peer-a-report.json"));
    RL_CHECK(std::filesystem::is_regular_file(output / "peer-b-report.json"));
    RL_CHECK(std::filesystem::is_regular_file(output / "peer-a-input.rlr"));
    RL_CHECK(std::filesystem::is_regular_file(output / "peer-b-input.rlr"));
    clean(output);
}

RL_TEST(udp_demo_port_conflict_fails_closed) {
    const auto held = UdpSocket::bind_loopback(0U);
    RL_REQUIRE(held.ok());
    const auto output = temp_output("rollback_lab_udp_port_conflict");
    clean(output);
    UdpDemoConfig config{};
    config.executable_path = executable_path();
    config.output_directory = output;
    config.frame_count = 30U;
    config.watchdog_milliseconds = 800U;
    config.forced_relay_port = held.value().local_port();
    const auto result = run_udp_demo(config);
    RL_CHECK(!result.ok());
    RL_CHECK(result.error().code == ErrorCode::child_failure ||
             result.error().code == ErrorCode::timeout);
    clean(output);
}

RL_TEST(udp_demo_missing_peer_times_out_and_reaps_children) {
    const auto output = temp_output("rollback_lab_udp_missing_peer");
    clean(output);
    UdpDemoConfig config{};
    config.executable_path = executable_path();
    config.output_directory = output;
    config.frame_count = 30U;
    config.watchdog_milliseconds = 900U;
    config.launch_peer_b = false;
    const auto result = run_udp_demo(config);
    RL_CHECK(!result.ok());
    RL_CHECK(result.error().code == ErrorCode::timeout ||
             result.error().code == ErrorCode::child_failure);
    clean(output);
}

RL_TEST(udp_demo_protocol_mismatch_returns_nonzero_and_reaps_children) {
    const auto output = temp_output("rollback_lab_udp_protocol_mismatch");
    clean(output);
    UdpDemoConfig config{};
    config.executable_path = executable_path();
    config.output_directory = output;
    config.frame_count = 30U;
    config.watchdog_milliseconds = 1'200U;
    config.peer_b_protocol_version = 99U;
    const auto result = run_udp_demo(config);
    RL_CHECK(!result.ok());
    RL_CHECK(result.error().code == ErrorCode::child_failure ||
             result.error().code == ErrorCode::timeout);
    clean(output);
}

RL_TEST(udp_demo_simulation_version_mismatch_returns_nonzero) {
    const auto output = temp_output("rollback_lab_udp_simulation_mismatch");
    clean(output);
    UdpDemoConfig config{};
    config.executable_path = executable_path();
    config.output_directory = output;
    config.frame_count = 30U;
    config.watchdog_milliseconds = 1'200U;
    config.peer_b_simulation_version = 99U;
    const auto result = run_udp_demo(config);
    RL_CHECK(!result.ok());
    RL_CHECK(result.error().code == ErrorCode::child_failure ||
             result.error().code == ErrorCode::timeout);
    clean(output);
}

RL_TEST(child_process_wait_is_idempotent_after_reap) {
    auto child_result = ChildProcess::spawn(executable_path(), {"--help"});
    RL_REQUIRE(child_result.ok());
    auto child = std::move(child_result.value());
    const auto first = child.wait_for(std::chrono::milliseconds{2'000});
    RL_REQUIRE(first.ok());
    RL_REQUIRE(first.value().has_value());
    RL_CHECK(first.value().value() == 0);
    const auto repeated = child.wait_for(std::chrono::milliseconds{0});
    RL_REQUIRE(repeated.ok());
    RL_REQUIRE(repeated.value().has_value());
    RL_CHECK(repeated.value().value() == 0);
}
