#pragma once

#include <rollback_lab/core/error.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace rollback_lab {

class ChildProcess final {
public:
    static auto spawn(const std::filesystem::path& executable,
                      const std::vector<std::string>& arguments)
        -> Result<ChildProcess>;

    ~ChildProcess();
    ChildProcess(const ChildProcess&) = delete;
    auto operator=(const ChildProcess&) -> ChildProcess& = delete;
    ChildProcess(ChildProcess&&) noexcept;
    auto operator=(ChildProcess&&) noexcept -> ChildProcess&;

    [[nodiscard]] auto id() const noexcept -> std::uint32_t;
    [[nodiscard]] auto wait_for(std::chrono::milliseconds timeout)
        -> Result<std::optional<int>>;
    [[nodiscard]] auto terminate() -> Result<void>;

private:
    struct Impl;
    explicit ChildProcess(std::unique_ptr<Impl> impl);
    void cleanup() noexcept;
    std::unique_ptr<Impl> impl_;
};

}  // namespace rollback_lab
