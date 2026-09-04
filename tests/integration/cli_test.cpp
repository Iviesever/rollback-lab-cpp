#include "test_framework.hpp"

#include <rollback_lab/cli/commands.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

using namespace rollback_lab;

auto call(std::initializer_list<std::string> arguments) -> int {
    return run_cli(std::vector<std::string>{arguments});
}

}  // namespace

RL_TEST(cli_simulate_replay_verify_benchmark_and_compare_use_real_artifacts) {
    const auto output = std::filesystem::temp_directory_path() /
                        "rollback_lab_cli_contract";
    std::error_code error;
    std::filesystem::remove_all(output, error);

    RL_CHECK(call({"rollback_lab", "simulate", "--scenario", "default",
                   "--frames", "120", "--out", output.string()}) == 0);
    const auto report = output / "report.json";
    const auto replay = output / "input.rlr";
    const auto trace = output / "trace.json";
    const auto viewer = output / "viewer.html";
    const auto diagnostic = output / "desync-diagnostic.json";
    RL_CHECK(std::filesystem::is_regular_file(report));
    RL_CHECK(std::filesystem::is_regular_file(replay));
    RL_CHECK(std::filesystem::is_regular_file(trace));
    RL_CHECK(std::filesystem::is_regular_file(viewer));
    RL_CHECK(std::filesystem::file_size(report) > 100U);
    RL_CHECK(std::filesystem::file_size(replay) > 100U);
    RL_CHECK(std::filesystem::file_size(trace) > 100U);
    RL_CHECK(std::filesystem::file_size(viewer) >
             std::filesystem::file_size(trace));

    RL_CHECK(call({"rollback_lab", "replay", replay.string()}) == 0);
    RL_CHECK(call({"rollback_lab", "verify", "--frames", "120"}) == 0);
    RL_CHECK(call({"rollback_lab", "benchmark", "--frames", "1000"}) == 0);
    RL_CHECK(call({"rollback_lab", "desync-demo", "--out",
                   diagnostic.string()}) == 0);
    RL_CHECK(std::filesystem::is_regular_file(diagnostic));
    RL_CHECK(std::filesystem::file_size(diagnostic) > 100U);
    RL_CHECK(call({"rollback_lab", "compare", report.string(),
                   report.string()}) == 0);

    std::filesystem::remove_all(output, error);
    RL_CHECK(!error);
}

RL_TEST(cli_simulate_accepts_full_width_seed_identity_and_rejects_overflow) {
    const auto output = std::filesystem::temp_directory_path() / "rollback_lab_cli_seeds";
    RL_REQUIRE(call({"rollback_lab", "simulate", "--frames", "30",
                     "--scenario-seed", "18446744073709551615", "--transport-seed", "4294967301",
                     "--out", output.string()}) == 0);
    std::ifstream stream{output / "report.json"};
    const std::string json{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
    RL_CHECK(json.find("\"scenario_seed\":18446744073709551615") != std::string::npos);
    RL_CHECK(json.find("\"transport_seed\":4294967301") != std::string::npos);
    RL_CHECK(call({"rollback_lab", "simulate", "--frames", "1", "--scenario-seed",
                   "18446744073709551616", "--out", output.string()}) != 0);
    RL_CHECK(call({"rollback_lab", "simulate", "--frames", "1", "--out", output.string(),
                   "--scenario-seed"}) == 2);
    RL_CHECK(call({"rollback_lab", "simulate", "--frames", "1", "--out", output.string(),
                   "--transport-seed", ""}) == 2);
    stream.close();
    std::filesystem::remove_all(output);
}
