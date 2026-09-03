#include <rollback_lab/transport/process.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <csignal>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace rollback_lab {
namespace {

#if defined(_WIN32)
auto utf8_to_wide(const std::string& value) -> std::wstring {
    if (value.empty()) {
        return {};
    }
    const auto count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                            value.data(),
                                            static_cast<int>(value.size()),
                                            nullptr, 0);
    if (count <= 0) {
        return {};
    }
    std::wstring wide(static_cast<std::size_t>(count), L'\0');
    static_cast<void>(MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), wide.data(), count));
    return wide;
}

auto quote_argument(const std::wstring& argument) -> std::wstring {
    if (argument.find_first_of(L" \t\"") == std::wstring::npos) {
        return argument;
    }
    std::wstring quoted{L'"'};
    std::size_t backslashes = 0U;
    for (const auto character : argument) {
        if (character == L'\\') {
            ++backslashes;
            continue;
        }
        if (character == L'"') {
            quoted.append(backslashes * 2U + 1U, L'\\');
            quoted.push_back(L'"');
            backslashes = 0U;
            continue;
        }
        quoted.append(backslashes, L'\\');
        backslashes = 0U;
        quoted.push_back(character);
    }
    quoted.append(backslashes * 2U, L'\\');
    quoted.push_back(L'"');
    return quoted;
}
#endif

}  // namespace

struct ChildProcess::Impl final {
#if defined(_WIN32)
    HANDLE process{nullptr};
    std::uint32_t pid{};
#else
    pid_t pid{-1};
#endif
    bool running{true};
};

ChildProcess::ChildProcess(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

ChildProcess::~ChildProcess() {
    if (impl_ != nullptr && impl_->running) {
        static_cast<void>(terminate());
        static_cast<void>(wait_for(std::chrono::milliseconds{500}));
    }
#if defined(_WIN32)
    if (impl_ != nullptr && impl_->process != nullptr) {
        CloseHandle(impl_->process);
    }
#endif
}

ChildProcess::ChildProcess(ChildProcess&&) noexcept = default;
auto ChildProcess::operator=(ChildProcess&&) noexcept -> ChildProcess& = default;

auto ChildProcess::spawn(const std::filesystem::path& executable,
                         const std::vector<std::string>& arguments)
    -> Result<ChildProcess> {
#if defined(_WIN32)
    std::wstring command = quote_argument(executable.wstring());
    for (const auto& argument : arguments) {
        const auto wide = utf8_to_wide(argument);
        if (!argument.empty() && wide.empty()) {
            return Result<ChildProcess>::failure(
                Error{ErrorCode::invalid_argument, 0U, 0U, "process_argument"});
        }
        command.push_back(L' ');
        command += quote_argument(wide);
    }
    command.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const auto working_directory = executable.parent_path().wstring();
    const BOOL created = CreateProcessW(
        executable.wstring().c_str(), command.data(), nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW, nullptr,
        working_directory.empty() ? nullptr : working_directory.c_str(),
        &startup, &process);
    if (created == FALSE) {
        return Result<ChildProcess>::failure(
            Error{ErrorCode::child_failure, GetLastError(), 0U,
                  "CreateProcessW"});
    }
    CloseHandle(process.hThread);
    auto impl = std::make_unique<Impl>();
    impl->process = process.hProcess;
    impl->pid = process.dwProcessId;
    return Result<ChildProcess>::success(ChildProcess{std::move(impl)});
#else
    std::vector<std::string> storage;
    storage.reserve(arguments.size() + 1U);
    storage.push_back(executable.string());
    storage.insert(storage.end(), arguments.begin(), arguments.end());
    std::vector<char*> argv;
    argv.reserve(storage.size() + 1U);
    for (auto& argument : storage) {
        argv.push_back(argument.data());
    }
    argv.push_back(nullptr);
    pid_t pid = -1;
    const auto result = posix_spawn(&pid, executable.c_str(), nullptr, nullptr,
                                    argv.data(), environ);
    if (result != 0) {
        return Result<ChildProcess>::failure(
            Error{ErrorCode::child_failure, static_cast<std::uint64_t>(result),
                  0U, "posix_spawn"});
    }
    auto impl = std::make_unique<Impl>();
    impl->pid = pid;
    return Result<ChildProcess>::success(ChildProcess{std::move(impl)});
#endif
}

auto ChildProcess::id() const noexcept -> std::uint32_t {
    if (impl_ == nullptr) {
        return 0U;
    }
#if defined(_WIN32)
    return impl_->pid;
#else
    return static_cast<std::uint32_t>(impl_->pid);
#endif
}

auto ChildProcess::wait_for(const std::chrono::milliseconds timeout)
    -> Result<std::optional<int>> {
    if (impl_ == nullptr || timeout.count() < 0) {
        return Result<std::optional<int>>::failure(
            Error{ErrorCode::invalid_argument, 0U, 0U, "process_wait"});
    }
    if (!impl_->running) {
        return Result<std::optional<int>>::failure(
            Error{ErrorCode::invalid_argument, 0U, 0U, "process_already_waited"});
    }
#if defined(_WIN32)
    const auto bounded_timeout = static_cast<DWORD>(
        std::min<std::int64_t>(timeout.count(), MAXDWORD - 1U));
    const auto waited = WaitForSingleObject(impl_->process, bounded_timeout);
    if (waited == WAIT_TIMEOUT) {
        return Result<std::optional<int>>::success(std::nullopt);
    }
    if (waited != WAIT_OBJECT_0) {
        return Result<std::optional<int>>::failure(
            Error{ErrorCode::child_failure, GetLastError(), 0U,
                  "WaitForSingleObject"});
    }
    DWORD exit_code{};
    if (GetExitCodeProcess(impl_->process, &exit_code) == FALSE) {
        return Result<std::optional<int>>::failure(
            Error{ErrorCode::child_failure, GetLastError(), 0U,
                  "GetExitCodeProcess"});
    }
    impl_->running = false;
    return Result<std::optional<int>>::success(
        std::optional<int>{static_cast<int>(exit_code)});
#else
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;) {
        int status{};
        const auto waited = waitpid(impl_->pid, &status, WNOHANG);
        if (waited == impl_->pid) {
            impl_->running = false;
            const int exit_code = WIFEXITED(status) ? WEXITSTATUS(status)
                                                    : 128 + WTERMSIG(status);
            return Result<std::optional<int>>::success(
                std::optional<int>{exit_code});
        }
        if (waited < 0) {
            return Result<std::optional<int>>::failure(
                Error{ErrorCode::child_failure, 0U, 0U, "waitpid"});
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return Result<std::optional<int>>::success(std::nullopt);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{2});
    }
#endif
}

auto ChildProcess::terminate() -> Result<void> {
    if (impl_ == nullptr || !impl_->running) {
        return Result<void>::success();
    }
#if defined(_WIN32)
    if (TerminateProcess(impl_->process, 125U) == FALSE) {
        return Result<void>::failure(
            Error{ErrorCode::child_failure, GetLastError(), 0U,
                  "TerminateProcess"});
    }
#else
    if (kill(impl_->pid, SIGTERM) != 0) {
        return Result<void>::failure(
            Error{ErrorCode::child_failure, 0U, 0U, "kill"});
    }
#endif
    return Result<void>::success();
}

}  // namespace rollback_lab
