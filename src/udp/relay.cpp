#include <rollback_lab/udp/relay.hpp>

#include <rollback_lab/transport/udp_socket.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <span>

namespace rollback_lab {
namespace {

constexpr std::array stop_message{
    std::byte{'R'}, std::byte{'L'}, std::byte{'S'},
    std::byte{'T'}, std::byte{'O'}, std::byte{'P'},
};

auto is_stop(const std::span<const std::byte> bytes) -> bool {
    return bytes.size() == stop_message.size() &&
           std::equal(bytes.begin(), bytes.end(), stop_message.begin());
}

}  // namespace

auto run_relay(const RelayConfig& config) -> Result<int> {
    auto socket_result = UdpSocket::bind_loopback(config.relay_port);
    if (!socket_result.ok()) {
        return Result<int>::failure(socket_result.error());
    }
    auto socket = std::move(socket_result.value());
    std::ofstream ready{config.ready_file, std::ios::binary | std::ios::trunc};
    if (!ready) {
        return Result<int>::failure(
            Error{ErrorCode::io_error, 0U, 0U, "relay_ready_file"});
    }
    ready << socket.local_port() << '\n';
    ready.close();

    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds{config.maximum_runtime_milliseconds};
    while (std::chrono::steady_clock::now() < deadline) {
        const auto received = socket.receive_for(std::chrono::milliseconds{10});
        if (!received.ok()) {
            return Result<int>::failure(received.error());
        }
        if (!received.value().has_value()) {
            continue;
        }
        const auto& datagram = received.value().value();
        if (is_stop(datagram.bytes)) {
            return Result<int>::success(0);
        }
        std::uint16_t destination{};
        if (datagram.source_port == config.peer_a_port) {
            destination = config.peer_b_port;
        } else if (datagram.source_port == config.peer_b_port) {
            destination = config.peer_a_port;
        } else {
            continue;
        }
        const auto sent = socket.send_loopback(destination, datagram.bytes);
        if (!sent.ok()) {
            return Result<int>::failure(sent.error());
        }
    }
    return Result<int>::success(0);
}

}  // namespace rollback_lab
