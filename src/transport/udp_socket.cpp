#include <rollback_lab/transport/udp_socket.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <mstcpip.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace rollback_lab {
namespace {

#if defined(_WIN32)
using NativeSocket = SOCKET;
constexpr NativeSocket invalid_socket = INVALID_SOCKET;
constexpr DWORD udp_connection_reset_control = _WSAIOW(IOC_VENDOR, 12);

class WinsockRuntime final {
public:
    WinsockRuntime() {
        WSADATA data{};
        error_ = WSAStartup(MAKEWORD(2, 2), &data);
    }
    ~WinsockRuntime() {
        if (error_ == 0) {
            WSACleanup();
        }
    }
    [[nodiscard]] auto ready() const noexcept -> bool { return error_ == 0; }
    [[nodiscard]] auto error() const noexcept -> int { return error_; }
private:
    int error_{};
};

auto runtime() -> WinsockRuntime& {
    static WinsockRuntime instance;
    return instance;
}

auto socket_error() noexcept -> std::uint64_t {
    return static_cast<std::uint64_t>(WSAGetLastError());
}

void close_socket(const NativeSocket socket) noexcept {
    if (socket != invalid_socket) {
        closesocket(socket);
    }
}
#else
using NativeSocket = int;
constexpr NativeSocket invalid_socket = -1;

auto socket_error() noexcept -> std::uint64_t {
    return static_cast<std::uint64_t>(errno);
}

void close_socket(const NativeSocket socket) noexcept {
    if (socket != invalid_socket) {
        close(socket);
    }
}
#endif

auto io_failure(const char* context, const std::uint64_t detail)
    -> Result<void> {
    return Result<void>::failure(
        Error{ErrorCode::io_error, detail, 0U, context});
}

}  // namespace

struct UdpSocket::Impl final {
    NativeSocket socket{invalid_socket};
    std::uint16_t port{};

    ~Impl() { close_socket(socket); }
};

UdpSocket::UdpSocket(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
UdpSocket::~UdpSocket() = default;
UdpSocket::UdpSocket(UdpSocket&&) noexcept = default;
auto UdpSocket::operator=(UdpSocket&&) noexcept -> UdpSocket& = default;

auto UdpSocket::bind_loopback(const std::uint16_t port) -> Result<UdpSocket> {
#if defined(_WIN32)
    if (!runtime().ready()) {
        return Result<UdpSocket>::failure(
            Error{ErrorCode::io_error,
                  static_cast<std::uint64_t>(runtime().error()), 0U,
                  "WSAStartup"});
    }
#endif
    const auto handle = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (handle == invalid_socket) {
        return Result<UdpSocket>::failure(
            Error{ErrorCode::io_error, socket_error(), 0U, "socket"});
    }
#if defined(_WIN32)
    BOOL report_udp_reset = FALSE;
    DWORD control_bytes{};
    if (WSAIoctl(handle, udp_connection_reset_control, &report_udp_reset,
                 sizeof(report_udp_reset), nullptr, 0U, &control_bytes,
                 nullptr, nullptr) == SOCKET_ERROR) {
        const auto error = socket_error();
        close_socket(handle);
        return Result<UdpSocket>::failure(
            Error{ErrorCode::io_error, error, 0U, "SIO_UDP_CONNRESET"});
    }
#endif
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    if (::bind(handle, reinterpret_cast<const sockaddr*>(&address),
               static_cast<int>(sizeof(address))) != 0) {
        const auto error = socket_error();
        close_socket(handle);
        return Result<UdpSocket>::failure(
            Error{ErrorCode::io_error, error, 0U, "bind"});
    }
    sockaddr_in local{};
#if defined(_WIN32)
    int size = sizeof(local);
#else
    socklen_t size = sizeof(local);
#endif
    if (getsockname(handle, reinterpret_cast<sockaddr*>(&local), &size) != 0) {
        const auto error = socket_error();
        close_socket(handle);
        return Result<UdpSocket>::failure(
            Error{ErrorCode::io_error, error, 0U, "getsockname"});
    }
    auto impl = std::make_unique<Impl>();
    impl->socket = handle;
    impl->port = ntohs(local.sin_port);
    return Result<UdpSocket>::success(UdpSocket{std::move(impl)});
}

auto UdpSocket::local_port() const noexcept -> std::uint16_t {
    return impl_ == nullptr ? 0U : impl_->port;
}

auto UdpSocket::send_loopback(const std::uint16_t port,
                              const std::span<const std::byte> bytes) const
    -> Result<void> {
    if (impl_ == nullptr || bytes.size() > 65'507U) {
        return Result<void>::failure(
            Error{ErrorCode::invalid_length, bytes.size(), 0U, "udp_send"});
    }
    sockaddr_in destination{};
    destination.sin_family = AF_INET;
    destination.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    destination.sin_port = htons(port);
    const auto sent = sendto(
        impl_->socket, reinterpret_cast<const char*>(bytes.data()),
        static_cast<int>(bytes.size()), 0,
        reinterpret_cast<const sockaddr*>(&destination),
        static_cast<int>(sizeof(destination)));
    if (sent < 0 || static_cast<std::size_t>(sent) != bytes.size()) {
        return io_failure("sendto", socket_error());
    }
    return Result<void>::success();
}

auto UdpSocket::receive_for(const std::chrono::milliseconds timeout) const
    -> Result<std::optional<Datagram>> {
    if (impl_ == nullptr || timeout.count() < 0) {
        return Result<std::optional<Datagram>>::failure(
            Error{ErrorCode::invalid_argument, 0U, 0U, "udp_receive"});
    }
    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(impl_->socket, &readable);
    timeval wait{};
    wait.tv_sec = static_cast<long>(timeout.count() / 1'000);
    wait.tv_usec = static_cast<long>((timeout.count() % 1'000) * 1'000);
#if defined(_WIN32)
    const auto selected = select(0, &readable, nullptr, nullptr, &wait);
#else
    const auto selected = select(impl_->socket + 1, &readable, nullptr, nullptr,
                                 &wait);
#endif
    if (selected == 0) {
        return Result<std::optional<Datagram>>::success(std::nullopt);
    }
    if (selected < 0) {
        return Result<std::optional<Datagram>>::failure(
            Error{ErrorCode::io_error, socket_error(), 0U, "select"});
    }
    std::array<std::byte, 65'507U> buffer{};
    sockaddr_in source{};
#if defined(_WIN32)
    int source_size = sizeof(source);
#else
    socklen_t source_size = sizeof(source);
#endif
    const auto received = recvfrom(
        impl_->socket, reinterpret_cast<char*>(buffer.data()),
        static_cast<int>(buffer.size()), 0,
        reinterpret_cast<sockaddr*>(&source), &source_size);
    if (received < 0) {
#if defined(_WIN32)
        const auto error = WSAGetLastError();
        if (error == WSAECONNRESET) {
            return Result<std::optional<Datagram>>::success(std::nullopt);
        }
#endif
        return Result<std::optional<Datagram>>::failure(
            Error{ErrorCode::io_error, socket_error(), 0U, "recvfrom"});
    }
    Datagram datagram{};
    datagram.bytes.assign(buffer.begin(),
                          buffer.begin() + static_cast<std::ptrdiff_t>(received));
    datagram.source_port = ntohs(source.sin_port);
    return Result<std::optional<Datagram>>::success(
        std::optional<Datagram>{std::move(datagram)});
}

}  // namespace rollback_lab
