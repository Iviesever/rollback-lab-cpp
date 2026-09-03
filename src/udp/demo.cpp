#include <rollback_lab/udp/demo.hpp>

#include <rollback_lab/netcode/session.hpp>
#include <rollback_lab/replay/replay.hpp>
#include <rollback_lab/transport/process.hpp>
#include <rollback_lab/transport/udp_socket.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace rollback_lab {
namespace {

auto number(const std::uint64_t value) -> std::string {
    return std::to_string(value);
}

auto reserve_port() -> Result<std::uint16_t> {
    const auto socket = UdpSocket::bind_loopback(0U);
    if (!socket.ok()) {
        return Result<std::uint16_t>::failure(socket.error());
    }
    return Result<std::uint16_t>::success(socket.value().local_port());
}

auto reserve_distinct(const std::uint16_t first,
                      const std::uint16_t second = 0U)
    -> Result<std::uint16_t> {
    for (std::uint32_t attempt = 0U; attempt < 16U; ++attempt) {
        const auto port = reserve_port();
        if (!port.ok()) {
            return port;
        }
        if (port.value() != first && port.value() != second) {
            return port;
        }
    }
    return Result<std::uint16_t>::failure(
        Error{ErrorCode::capacity_exceeded, 16U, 0U, "udp_port_reservation"});
}

auto wait_for_ready(const std::filesystem::path& ready_file,
                    ChildProcess& relay,
                    const std::chrono::milliseconds timeout) -> Result<void> {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (std::filesystem::is_regular_file(ready_file)) {
            return Result<void>::success();
        }
        const auto exited = relay.wait_for(std::chrono::milliseconds{0});
        if (!exited.ok()) {
            return Result<void>::failure(exited.error());
        }
        if (exited.value().has_value()) {
            return Result<void>::failure(
                Error{ErrorCode::child_failure,
                      static_cast<std::uint64_t>(exited.value().value()), 0U,
                      "relay_start"});
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
    return Result<void>::failure(
        Error{ErrorCode::timeout,
              static_cast<std::uint64_t>(timeout.count()), 0U,
              "relay_ready"});
}

auto peer_arguments(const UdpDemoConfig& config,
                    const PlayerId id,
                    const std::uint16_t listen_port,
                    const std::uint16_t relay_port,
                    const std::filesystem::path& report,
                    const std::filesystem::path& replay,
                    const std::uint16_t protocol_version)
    -> std::vector<std::string> {
    const auto handshake = std::max<std::uint32_t>(
        200U, std::min<std::uint32_t>(1'000U,
                                     config.watchdog_milliseconds / 2U));
    return {
        "peer", "--id", id == PlayerId::a ? "A" : "B",
        "--listen-port", number(listen_port),
        "--relay-port", number(relay_port),
        "--scenario-seed", number(config.scenario_seed),
        "--transport-seed", number(config.transport_seed),
        "--frames", number(config.frame_count),
        "--protocol-version", number(protocol_version),
        "--handshake-ms", number(handshake),
        "--run-ms", number(config.watchdog_milliseconds),
        "--report", report.string(),
        "--replay", replay.string(),
    };
}

auto read_binary(const std::filesystem::path& path)
    -> Result<std::vector<std::byte>> {
    std::ifstream stream{path, std::ios::binary};
    if (!stream) {
        return Result<std::vector<std::byte>>::failure(
            Error{ErrorCode::io_error, 0U, 0U, "udp_read_replay"});
    }
    stream.seekg(0, std::ios::end);
    const auto length = stream.tellg();
    if (length < 0) {
        return Result<std::vector<std::byte>>::failure(
            Error{ErrorCode::io_error, 0U, 0U, "udp_replay_length"});
    }
    stream.seekg(0, std::ios::beg);
    std::vector<std::byte> bytes(static_cast<std::size_t>(length));
    if (!bytes.empty()) {
        stream.read(reinterpret_cast<char*>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size()));
    }
    if (!stream) {
        return Result<std::vector<std::byte>>::failure(
            Error{ErrorCode::io_error, bytes.size(), 0U, "udp_replay_read"});
    }
    return Result<std::vector<std::byte>>::success(std::move(bytes));
}

auto read_text(const std::filesystem::path& path) -> Result<std::string> {
    std::ifstream stream{path, std::ios::binary};
    if (!stream) {
        return Result<std::string>::failure(
            Error{ErrorCode::io_error, 0U, 0U, "udp_read_report"});
    }
    std::ostringstream text;
    text << stream.rdbuf();
    return Result<std::string>::success(text.str());
}

auto hash_text(const StateHash hash) -> std::string {
    std::ostringstream output;
    output << "0x" << std::hex << std::uppercase << std::setw(16)
           << std::setfill('0') << hash;
    return output.str();
}

auto report_matches(const std::string& report,
                    const std::uint32_t frame,
                    const StateHash hash) -> bool {
    return report.find("\"success\":true") != std::string::npos &&
           report.find("\"confirmed_frame\":" + number(frame)) !=
               std::string::npos &&
           report.find(hash_text(hash)) != std::string::npos;
}

auto stop_relay(UdpSocket& control, const std::uint16_t port) -> Result<void> {
    constexpr std::array message{
        std::byte{'R'}, std::byte{'L'}, std::byte{'S'},
        std::byte{'T'}, std::byte{'O'}, std::byte{'P'},
    };
    return control.send_loopback(port, message);
}

}  // namespace

auto run_udp_demo(const UdpDemoConfig& config) -> Result<UdpDemoResult> {
    if (config.executable_path.empty() || config.frame_count == 0U ||
        config.frame_count > kHistoryCapacity ||
        config.watchdog_milliseconds < 200U) {
        return Result<UdpDemoResult>::failure(
            Error{ErrorCode::invalid_argument, config.frame_count, 0U,
                  "udp_demo_config"});
    }
    std::error_code filesystem_error;
    std::filesystem::create_directories(config.output_directory,
                                        filesystem_error);
    if (filesystem_error) {
        return Result<UdpDemoResult>::failure(
            Error{ErrorCode::io_error,
                  static_cast<std::uint64_t>(filesystem_error.value()), 0U,
                  "udp_output_directory"});
    }

    const auto relay_port_result = config.forced_relay_port.has_value()
        ? Result<std::uint16_t>::success(config.forced_relay_port.value())
        : reserve_port();
    if (!relay_port_result.ok()) {
        return Result<UdpDemoResult>::failure(relay_port_result.error());
    }
    const auto relay_port = relay_port_result.value();
    const auto peer_a_port_result = reserve_distinct(relay_port);
    if (!peer_a_port_result.ok()) {
        return Result<UdpDemoResult>::failure(peer_a_port_result.error());
    }
    const auto peer_b_port_result =
        reserve_distinct(relay_port, peer_a_port_result.value());
    if (!peer_b_port_result.ok()) {
        return Result<UdpDemoResult>::failure(peer_b_port_result.error());
    }
    const auto peer_a_port = peer_a_port_result.value();
    const auto peer_b_port = peer_b_port_result.value();

    const auto ready_file = config.output_directory / "relay.ready";
    const auto report_a = config.output_directory / "peer-a-report.json";
    const auto report_b = config.output_directory / "peer-b-report.json";
    const auto replay_a = config.output_directory / "peer-a-input.rlr";
    const auto replay_b = config.output_directory / "peer-b-input.rlr";
    for (const auto& path : {ready_file, report_a, report_b, replay_a, replay_b}) {
        std::filesystem::remove(path, filesystem_error);
        filesystem_error.clear();
    }

    auto relay_result = ChildProcess::spawn(
        config.executable_path,
        {"relay", "--relay-port", number(relay_port),
         "--peer-a-port", number(peer_a_port),
         "--peer-b-port", number(peer_b_port),
         "--ready", ready_file.string(),
         "--max-ms", number(config.watchdog_milliseconds + 1'000U)});
    if (!relay_result.ok()) {
        return Result<UdpDemoResult>::failure(relay_result.error());
    }
    auto relay = std::move(relay_result.value());
    const auto ready = wait_for_ready(
        ready_file, relay,
        std::chrono::milliseconds{std::min<std::uint32_t>(
            config.watchdog_milliseconds, 1'000U)});
    if (!ready.ok()) {
        return Result<UdpDemoResult>::failure(ready.error());
    }

    auto peer_a_result = ChildProcess::spawn(
        config.executable_path,
        peer_arguments(config, PlayerId::a, peer_a_port, relay_port,
                       report_a, replay_a,
                       static_cast<std::uint16_t>(kProtocolVersion)));
    if (!peer_a_result.ok()) {
        return Result<UdpDemoResult>::failure(peer_a_result.error());
    }
    auto peer_a = std::move(peer_a_result.value());
    std::optional<ChildProcess> peer_b;
    if (config.launch_peer_b) {
        auto peer_b_result = ChildProcess::spawn(
            config.executable_path,
            peer_arguments(config, PlayerId::b, peer_b_port, relay_port,
                           report_b, replay_b,
                           config.peer_b_protocol_version));
        if (!peer_b_result.ok()) {
            return Result<UdpDemoResult>::failure(peer_b_result.error());
        }
        peer_b.emplace(std::move(peer_b_result.value()));
    }

    std::optional<int> exit_a;
    std::optional<int> exit_b;
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds{config.watchdog_milliseconds};
    while (std::chrono::steady_clock::now() < deadline &&
           (!exit_a.has_value() ||
            (config.launch_peer_b && !exit_b.has_value()))) {
        if (!exit_a.has_value()) {
            const auto waited = peer_a.wait_for(std::chrono::milliseconds{0});
            if (!waited.ok()) {
                return Result<UdpDemoResult>::failure(waited.error());
            }
            exit_a = waited.value();
        }
        if (peer_b.has_value() && !exit_b.has_value()) {
            const auto waited = peer_b->wait_for(std::chrono::milliseconds{0});
            if (!waited.ok()) {
                return Result<UdpDemoResult>::failure(waited.error());
            }
            exit_b = waited.value();
        }
        if (!exit_a.has_value() ||
            (config.launch_peer_b && !exit_b.has_value())) {
            std::this_thread::sleep_for(std::chrono::milliseconds{5});
        }
    }

    if (!exit_a.has_value() ||
        (config.launch_peer_b && !exit_b.has_value())) {
        return Result<UdpDemoResult>::failure(
            Error{ErrorCode::timeout, config.watchdog_milliseconds, 0U,
                  "udp_demo_watchdog"});
    }
    if (exit_a.value() != 0 || !config.launch_peer_b ||
        !exit_b.has_value() || exit_b.value() != 0) {
        const bool peer_a_failed = exit_a.value() != 0;
        return Result<UdpDemoResult>::failure(
            Error{ErrorCode::child_failure,
                  static_cast<std::uint64_t>(
                      peer_a_failed ? exit_a.value() : exit_b.value_or(-1)),
                  0U, peer_a_failed ? "udp_peer_a_exit" : "udp_peer_b_exit"});
    }

    auto control_result = UdpSocket::bind_loopback(0U);
    if (!control_result.ok()) {
        return Result<UdpDemoResult>::failure(control_result.error());
    }
    auto control = std::move(control_result.value());
    const auto stopped = stop_relay(control, relay_port);
    if (!stopped.ok()) {
        return Result<UdpDemoResult>::failure(stopped.error());
    }
    const auto relay_waited = relay.wait_for(std::chrono::milliseconds{1'000});
    if (!relay_waited.ok() || !relay_waited.value().has_value() ||
        relay_waited.value().value() != 0) {
        return Result<UdpDemoResult>::failure(
            Error{ErrorCode::child_failure, 0U, 0U, "udp_relay_exit"});
    }

    const auto bytes_a = read_binary(replay_a);
    const auto bytes_b = read_binary(replay_b);
    const auto text_a = read_text(report_a);
    const auto text_b = read_text(report_b);
    if (!bytes_a.ok() || !bytes_b.ok() || !text_a.ok() || !text_b.ok()) {
        return Result<UdpDemoResult>::failure(
            Error{ErrorCode::io_error, 0U, 0U, "udp_demo_artifacts"});
    }
    const auto decoded_a = decode_replay(bytes_a.value());
    const auto decoded_b = decode_replay(bytes_b.value());
    if (!decoded_a.ok() || !decoded_b.ok() ||
        decoded_a.value() != decoded_b.value()) {
        return Result<UdpDemoResult>::failure(
            Error{ErrorCode::replay_mismatch, 0U, 0U, "udp_replay_compare"});
    }
    const auto verified_a = verify_replay(decoded_a.value());
    const auto verified_b = verify_replay(decoded_b.value());
    const auto final_hash = decoded_a.value().expected_final_hash;
    if (!verified_a.ok() || !verified_b.ok() ||
        !report_matches(text_a.value(), config.frame_count, final_hash) ||
        !report_matches(text_b.value(), config.frame_count, final_hash)) {
        return Result<UdpDemoResult>::failure(
            Error{ErrorCode::replay_mismatch, final_hash, 0U,
                  "udp_report_validation"});
    }

    return Result<UdpDemoResult>::success(UdpDemoResult{
        relay.id(), peer_a.id(), peer_b->id(), relay_port, peer_a_port,
        peer_b_port, FrameNumber{config.frame_count}, final_hash, final_hash,
        true, relay_waited.value().value(), exit_a.value(), exit_b.value()});
}

}  // namespace rollback_lab
