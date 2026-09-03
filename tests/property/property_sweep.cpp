#include <rollback_lab/report/property_sweep.hpp>

#include <charconv>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

namespace {

auto value_after(const int argc, char** argv, const std::string_view option)
    -> std::string {
    for (int index = 1; index + 1 < argc; ++index) {
        if (argv[index] == option) {
            return argv[index + 1];
        }
    }
    return {};
}

auto parse(const std::string& text, const std::uint32_t fallback)
    -> std::uint32_t {
    if (text.empty()) {
        return fallback;
    }
    std::uint32_t value{};
    const auto result = std::from_chars(text.data(), text.data() + text.size(),
                                        value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size()
               ? value
               : 0U;
}

}  // namespace

int main(const int argc, char** argv) {
    rollback_lab::PropertySweepConfig config{};
    config.seed_count = parse(value_after(argc, argv, "--seeds"), 100U);
    config.repeat_identity_samples =
        parse(value_after(argc, argv, "--repeat-samples"), 16U);
    if (config.seed_count == 0U ||
        config.repeat_identity_samples > config.seed_count) {
        std::cerr << "invalid property sweep arguments\n";
        return 2;
    }
    const auto result = rollback_lab::run_property_sweep(config);
    if (!result.ok()) {
        std::cerr << "property sweep failed: code "
                  << static_cast<unsigned>(result.error().code)
                  << ", seed " << result.error().detail
                  << ", context " << result.error().context << '\n';
        return 3;
    }
    if (result.value().total_seeds != config.seed_count ||
        result.value().successful_seeds + result.value().declared_failures !=
            config.seed_count ||
        result.value().crashes != 0U || result.value().deadlocks != 0U ||
        result.value().unbounded_failures != 0U ||
        result.value().identity_mismatches != 0U) {
        std::cerr << "property sweep invariant failure\n";
        return 4;
    }
    const auto json = rollback_lab::canonical_json(result.value());
    std::cout << json;
    const auto output = value_after(argc, argv, "--out");
    if (!output.empty()) {
        std::ofstream file{output, std::ios::binary | std::ios::trunc};
        file << json;
        if (!file) {
            return 5;
        }
    }
    return 0;
}

