#include "test_framework.hpp"

#include <iostream>

int main() {
    int failures = 0;
    for (const auto& test_case : rollback_lab::test::registry()) {
        try {
            test_case.function();
            std::cout << "[PASS] " << test_case.name << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "[FAIL] " << test_case.name << ": " << error.what()
                      << '\n';
        } catch (...) {
            ++failures;
            std::cerr << "[FAIL] " << test_case.name
                      << ": unknown exception\n";
        }
    }

    std::cout << rollback_lab::test::registry().size() << " tests, "
              << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}

