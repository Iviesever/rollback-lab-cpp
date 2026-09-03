#pragma once

#include <exception>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace rollback_lab::test {

struct Case final {
    std::string_view name;
    void (*function)();
};

inline auto registry() -> std::vector<Case>& {
    static std::vector<Case> cases;
    return cases;
}

struct Registrar final {
    Registrar(const std::string_view name, void (*function)()) {
        registry().push_back(Case{name, function});
    }
};

inline void check(const bool condition,
                  const std::string_view expression,
                  const std::string_view file,
                  const int line) {
    if (condition) {
        return;
    }
    std::ostringstream message;
    message << file << ':' << line << ": check failed: " << expression;
    throw std::runtime_error(message.str());
}

}  // namespace rollback_lab::test

#define RL_TEST(name)                                                          \
    static void name();                                                        \
    static const ::rollback_lab::test::Registrar name##_registrar{#name, name}; \
    static void name()

#define RL_CHECK(expression)                                                   \
    ::rollback_lab::test::check(static_cast<bool>(expression), #expression,    \
                                __FILE__, __LINE__)

#define RL_REQUIRE(expression) RL_CHECK(expression)

