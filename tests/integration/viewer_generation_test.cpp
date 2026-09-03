#include "test_framework.hpp"

#include <rollback_lab/report/canonical_json.hpp>
#include <rollback_lab/report/scenario_runner.hpp>
#include <rollback_lab/report/viewer.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

using namespace rollback_lab;

auto read_text(const std::filesystem::path& path) -> std::string {
    std::ifstream stream{path, std::ios::binary};
    std::ostringstream text;
    text << stream.rdbuf();
    return text.str();
}

}  // namespace

RL_TEST(viewer_is_self_contained_and_embeds_the_real_trace) {
    ScenarioRunConfig config{};
    config.scenario_seed = 314159U;
    config.transport_seed = 271828U;
    config.frame_count = 90U;
    config.transport.base_latency_ticks = 4U;
    config.transport.jitter_ticks = 2U;
    config.transport.loss_percent = 5U;
    config.transport.reorder_percent = 15U;
    config.transport.duplicate_percent = 5U;
    const auto run = run_seeded_scenario(config);
    RL_REQUIRE(run.ok());
    const auto trace_json = canonical_json(run.value().trace);
    const auto viewer = generate_viewer_html(run.value().trace);
    RL_REQUIRE(trace_json.ok());
    RL_REQUIRE(viewer.ok());

    RL_CHECK(viewer.value().find("const TRACE = " + trace_json.value()) !=
             std::string::npos);
    RL_CHECK(viewer.value().find("https://") == std::string::npos);
    RL_CHECK(viewer.value().find("http://") == std::string::npos);
    RL_CHECK(viewer.value().find("<script src=") == std::string::npos);
    RL_CHECK(viewer.value().find("fetch(") == std::string::npos);
    RL_CHECK(viewer.value().find("id=\"arena\"") != std::string::npos);
    RL_CHECK(viewer.value().find("id=\"play-toggle\"") != std::string::npos);
    RL_CHECK(viewer.value().find("id=\"step-back\"") != std::string::npos);
    RL_CHECK(viewer.value().find("id=\"step-forward\"") != std::string::npos);
    RL_CHECK(viewer.value().find("id=\"timeline\"") != std::string::npos);
    RL_CHECK(viewer.value().find("id=\"packet-lane\"") != std::string::npos);
    RL_CHECK(viewer.value().find("id=\"rollback-lane\"") != std::string::npos);
    RL_CHECK(viewer.value().find("window.rollbackViewer") != std::string::npos);
    RL_CHECK(viewer.value().find("transition: all") == std::string::npos);
    RL_CHECK(viewer.value().find("prefers-reduced-motion") != std::string::npos);
    RL_CHECK(viewer.value().size() < 5U * 1024U * 1024U);
}

RL_TEST(viewer_writer_rejects_empty_trace_and_writes_valid_html) {
    const auto empty = generate_viewer_html(Trace{});
    RL_CHECK(!empty.ok());
    RL_CHECK(empty.error().code == ErrorCode::invalid_argument);

    ScenarioRunConfig config{};
    config.frame_count = 30U;
    const auto run = run_seeded_scenario(config);
    RL_REQUIRE(run.ok());
    const auto path = std::filesystem::temp_directory_path() /
                      "rollback-lab-viewer-contract.html";
    const auto written = write_viewer(run.value().trace, path);
    RL_REQUIRE(written.ok());
    const auto html = read_text(path);
    RL_CHECK(html.starts_with("<!doctype html>"));
    RL_CHECK(html.find("Rollback Timeline") != std::string::npos);
    std::error_code error;
    std::filesystem::remove(path, error);
    RL_CHECK(!error);
}

RL_TEST(trace_sampling_interval_reserves_the_initial_frame_and_stays_bounded) {
    RL_CHECK(trace_sample_interval(1U) == 1U);
    RL_CHECK(trace_sample_interval(9'999U) == 1U);
    RL_CHECK(trace_sample_interval(10'000U) == 2U);
    RL_CHECK(trace_sample_interval(1'000'000U) == 101U);
}
