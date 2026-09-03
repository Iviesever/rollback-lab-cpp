#pragma once

#include <rollback_lab/core/error.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace rollback_lab {

struct Datagram final {
    std::vector<std::byte> bytes;
    std::uint16_t source_port{};
};

class UdpSocket final {
public:
    static auto bind_loopback(std::uint16_t port) -> Result<UdpSocket>;

    ~UdpSocket();
    UdpSocket(const UdpSocket&) = delete;
    auto operator=(const UdpSocket&) -> UdpSocket& = delete;
    UdpSocket(UdpSocket&&) noexcept;
    auto operator=(UdpSocket&&) noexcept -> UdpSocket&;

    [[nodiscard]] auto local_port() const noexcept -> std::uint16_t;
    [[nodiscard]] auto send_loopback(std::uint16_t port,
                                     std::span<const std::byte> bytes) const
        -> Result<void>;
    [[nodiscard]] auto receive_for(std::chrono::milliseconds timeout) const
        -> Result<std::optional<Datagram>>;

private:
    struct Impl;
    explicit UdpSocket(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

}  // namespace rollback_lab

