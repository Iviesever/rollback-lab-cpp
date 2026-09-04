#include <rollback_lab/cli/commands.hpp>

#include <rollback_lab/replay/replay.hpp>
#include <rollback_lab/report/canonical_json.hpp>
#include <rollback_lab/report/scenario_runner.hpp>
#include <rollback_lab/report/viewer.hpp>
#include <rollback_lab/simulation/scripted_input.hpp>
#include <rollback_lab/udp/demo.hpp>
#include <rollback_lab/udp/peer.hpp>
#include <rollback_lab/udp/relay.hpp>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rollback_lab {
namespace {

void print_help() {
    std::cout
        << "rollback_lab <command>\n"
        << "  simulate --scenario default [--frames N] [--out DIRECTORY]\n"
        << "    [--scenario-seed uint64] [--transport-seed uint64]\n"
        << "  replay FILE\n"
        << "  udp-demo [--frames N] [--out DIRECTORY] [--inject-desync]\n"
        << "  verify [--frames N]\n"
        << "  benchmark [--frames N]\n"
        << "  desync-demo [--out FILE]\n"
        << "  compare REPORT_A REPORT_B\n";
}

auto option_value(const std::span<const std::string> arguments,
                  const std::string_view option) -> std::string {
    for (std::size_t index = 0U; index + 1U < arguments.size(); ++index) {
        if (arguments[index] == option) {
            return arguments[index + 1U];
        }
    }
    return {};
}

auto parse_u32(const std::string& text, const std::uint32_t fallback)
    -> Result<std::uint32_t> {
    if (text.empty()) {
        return Result<std::uint32_t>::success(fallback);
    }
    std::uint32_t value{};
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
        return Result<std::uint32_t>::failure(
            Error{ErrorCode::invalid_argument, 0U, 0U, "numeric_option"});
    }
    return Result<std::uint32_t>::success(value);
}

auto parse_u64(const std::span<const std::string> arguments,
               const std::string_view option, const std::uint64_t fallback)
    -> Result<std::uint64_t> {
    const auto found = std::find(arguments.begin(), arguments.end(), option);
    if (found == arguments.end()) return Result<std::uint64_t>::success(fallback);
    const auto supplied = std::next(found);
    if (supplied == arguments.end() || supplied->empty()) {
        return Result<std::uint64_t>::failure(
            Error{ErrorCode::invalid_argument, 0U, 0U, "missing_seed_option"});
    }
    const auto& text = *supplied;
    std::uint64_t value{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
        return Result<std::uint64_t>::failure(
            Error{ErrorCode::invalid_argument, 0U, 0U, "seed_option"});
    }
    return Result<std::uint64_t>::success(value);
}

auto read_binary(const std::filesystem::path& path)
    -> Result<std::vector<std::byte>> {
    std::ifstream stream{path, std::ios::binary};
    if (!stream) {
        return Result<std::vector<std::byte>>::failure(
            Error{ErrorCode::io_error, 0U, 0U, "open_input"});
    }
    stream.seekg(0, std::ios::end);
    const auto length = stream.tellg();
    if (length < 0 || static_cast<std::uint64_t>(length) >
                          static_cast<std::uint64_t>(
                              std::numeric_limits<std::size_t>::max())) {
        return Result<std::vector<std::byte>>::failure(
            Error{ErrorCode::invalid_length, 0U, 0U, "input_file"});
    }
    stream.seekg(0, std::ios::beg);
    std::vector<std::byte> bytes(static_cast<std::size_t>(length));
    if (!bytes.empty()) {
        stream.read(reinterpret_cast<char*>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size()));
    }
    if (!stream) {
        return Result<std::vector<std::byte>>::failure(
            Error{ErrorCode::io_error, bytes.size(), 0U, "read_input"});
    }
    return Result<std::vector<std::byte>>::success(std::move(bytes));
}

auto read_text(const std::filesystem::path& path) -> Result<std::string> {
    std::ifstream stream{path, std::ios::binary};
    if (!stream) {
        return Result<std::string>::failure(
            Error{ErrorCode::io_error, 0U, 0U, "open_report"});
    }
    return Result<std::string>::success(std::string{
        std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}});
}

auto write_binary(const std::filesystem::path& path,
                  const std::span<const std::byte> bytes) -> bool {
    std::ofstream stream{path, std::ios::binary | std::ios::trunc};
    if (!stream) {
        return false;
    }
    if (!bytes.empty()) {
        stream.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
    }
    return static_cast<bool>(stream);
}

auto write_text(const std::filesystem::path& path,
                const std::string& text) -> bool {
    std::ofstream stream{path, std::ios::binary | std::ios::trunc};
    stream << text;
    return static_cast<bool>(stream);
}

auto default_scenario(const std::uint32_t frames) -> ScenarioRunConfig {
    ScenarioRunConfig config{};
    config.scenario_seed = 0xC0FFEEU;
    config.transport_seed = 0x51A7E5U;
    config.frame_count = frames;
    config.transport.seed = config.transport_seed;
    config.transport.base_latency_ticks = 5U;
    config.transport.jitter_ticks = 3U;
    config.transport.loss_percent = 5U;
    config.transport.reorder_percent = 10U;
    config.transport.duplicate_percent = 5U;
    config.transport.burst_loss_percent = 1U;
    config.transport.max_queue_packets = 4'096U;
    config.transport.max_queue_bytes = 4U << 20U;
    return config;
}

auto simulate_command(const std::span<const std::string> arguments) -> int {
    const auto scenario = option_value(arguments, "--scenario");
    if (!scenario.empty() && scenario != "default") {
        std::cerr << "unsupported scenario: " << scenario << '\n';
        return 2;
    }
    const auto frames = parse_u32(option_value(arguments, "--frames"), 600U);
    if (!frames.ok()) {
        std::cerr << "invalid --frames\n";
        return 2;
    }
    const auto output_text = option_value(arguments, "--out");
    const auto output = output_text.empty()
                            ? std::filesystem::path{"artifacts/latest"}
                            : std::filesystem::path{output_text};
    std::error_code error;
    std::filesystem::create_directories(output, error);
    if (error) {
        std::cerr << "cannot create output directory\n";
        return 3;
    }

    auto config = default_scenario(frames.value());
    const auto scenario_seed = parse_u64(arguments, "--scenario-seed", config.scenario_seed);
    const auto transport_seed = parse_u64(arguments, "--transport-seed", config.transport_seed);
    if (!scenario_seed.ok() || !transport_seed.ok()) {
        std::cerr << "invalid scenario or transport seed\n";
        return 2;
    }
    config.scenario_seed = scenario_seed.value();
    config.transport_seed = transport_seed.value();
    const auto artifacts = run_seeded_scenario(config);
    if (!artifacts.ok()) {
        std::cerr << "simulation failed with error code "
                  << static_cast<unsigned>(artifacts.error().code) << '\n';
        return 4;
    }
    const auto report = canonical_json(artifacts.value().report);
    const auto trace = canonical_json(artifacts.value().trace);
    const auto replay = encode_replay(artifacts.value().replay);
    if (!report.ok() || !trace.ok() || !replay.ok() ||
        !write_text(output / "report.json", report.value()) ||
        !write_text(output / "trace.json", trace.value()) ||
        !write_binary(output / "input.rlr", replay.value()) ||
        !write_viewer(artifacts.value().trace, output / "viewer.html").ok()) {
        std::cerr << "artifact write failed\n";
        return 5;
    }
    std::cout << report.value();
    return 0;
}

auto replay_command(const std::span<const std::string> arguments) -> int {
    if (arguments.size() < 3U) {
        std::cerr << "replay requires a file\n";
        return 2;
    }
    const auto bytes = read_binary(arguments[2]);
    if (!bytes.ok()) {
        std::cerr << "cannot read replay\n";
        return 3;
    }
    const auto replay = decode_replay(bytes.value());
    if (!replay.ok()) {
        std::cerr << "replay decode failed\n";
        return 4;
    }
    const auto verified = verify_replay(replay.value());
    if (!verified.ok()) {
        std::cerr << "replay verification failed\n";
        return 5;
    }
    std::cout << "replay verified: frame " << verified.value().final_frame.value
              << ", hash " << verified.value().actual_final_hash << '\n';
    return 0;
}

auto verify_command(const std::span<const std::string> arguments) -> int {
    const auto frames = parse_u32(option_value(arguments, "--frames"), 600U);
    if (!frames.ok()) {
        return 2;
    }
    const auto artifacts = run_seeded_scenario(default_scenario(frames.value()));
    if (!artifacts.ok()) {
        std::cerr << "verification failed\n";
        return 3;
    }
    std::cout << "verification passed: " << frames.value()
              << " frames, identity "
              << report_identity(artifacts.value().report) << '\n';
    return 0;
}

auto benchmark_command(const std::span<const std::string> arguments) -> int {
    const auto frames = parse_u32(option_value(arguments, "--frames"), 100'000U);
    if (!frames.ok() || frames.value() == 0U) {
        return 2;
    }
    using Clock = std::chrono::steady_clock;
    auto state = make_initial_world();
    const auto simulation_start = Clock::now();
    for (std::uint32_t frame = 0U; frame < frames.value(); ++frame) {
        const auto number = FrameNumber{frame};
        const auto next = simulate_frame(
            state, number,
            InputPair{scripted_input(1U, number, PlayerId::a),
                      scripted_input(1U, number, PlayerId::b)});
        if (!next.ok()) {
            return 3;
        }
        state = next.value();
    }
    const auto simulation_end = Clock::now();
    const auto simulation_us = std::max<std::int64_t>(
        1, std::chrono::duration_cast<std::chrono::microseconds>(
               simulation_end - simulation_start).count());

    auto stress_config = default_scenario(
        std::min<std::uint32_t>(frames.value(), 2'000U));
    stress_config.transport.base_latency_ticks = 8U;
    stress_config.transport.jitter_ticks = 4U;
    stress_config.transport.loss_percent = 0U;
    const auto rollback_start = Clock::now();
    const auto stress = run_seeded_scenario(stress_config);
    const auto rollback_end = Clock::now();
    if (!stress.ok()) {
        return 4;
    }
    const auto rollback_us = std::max<std::int64_t>(
        1, std::chrono::duration_cast<std::chrono::microseconds>(
               rollback_end - rollback_start).count());
    const auto ticks_per_second =
        static_cast<std::uint64_t>(frames.value()) * 1'000'000ULL /
        static_cast<std::uint64_t>(simulation_us);
    std::cout << "{\"simulation\":{\"total_ticks\":" << frames.value()
              << ",\"total_microseconds\":" << simulation_us
              << ",\"ticks_per_second\":" << ticks_per_second
              << "},\"rollback_stress\":{\"total_ticks\":"
              << stress_config.frame_count << ",\"total_microseconds\":"
              << rollback_us << ",\"rollback_count\":"
              << stress.value().report.rollback_count
              << ",\"resimulated_frames\":"
              << stress.value().report.resimulated_frames
              << ",\"maximum_depth\":"
              << stress.value().report.maximum_rollback_depth << "}}\n";
    return 0;
}

auto identity_field(const std::string& report) -> std::string {
    constexpr std::string_view marker = "\"identity_digest\":\"";
    const auto begin = report.find(marker);
    if (begin == std::string::npos) {
        return {};
    }
    const auto value_begin = begin + marker.size();
    const auto end = report.find('"', value_begin);
    return end == std::string::npos
               ? std::string{}
               : report.substr(value_begin, end - value_begin);
}

auto compare_command(const std::span<const std::string> arguments) -> int {
    if (arguments.size() < 4U) {
        std::cerr << "compare requires two reports\n";
        return 2;
    }
    const auto left = read_text(arguments[2]);
    const auto right = read_text(arguments[3]);
    if (!left.ok() || !right.ok()) {
        return 3;
    }
    const auto left_identity = identity_field(left.value());
    const auto right_identity = identity_field(right.value());
    if (left_identity.empty() || left_identity != right_identity) {
        std::cerr << "reports differ\n";
        return 4;
    }
    std::cout << "reports match: " << left_identity << '\n';
    return 0;
}

auto desync_demo_command(const std::span<const std::string> arguments) -> int {
    auto canonical = make_initial_world();
    canonical.players[0].x = 100 * kSubunitsPerWorldUnit;
    canonical.players[0].y = 100 * kSubunitsPerWorldUnit;
    canonical.players[1].x = 140 * kSubunitsPerWorldUnit;
    canonical.players[1].y = 100 * kSubunitsPerWorldUnit;
    const InputPair inputs{
        InputFrame{FrameNumber{0U}, PlayerId::a, 0U,
                   button_mask(Button::attack)},
        InputFrame{FrameNumber{0U}, PlayerId::b, 0U, 0U}};
    const auto expected = simulate_frame(canonical, FrameNumber{0U}, inputs);
    const auto divergent = simulate_frame(canonical, FrameNumber{0U}, inputs,
                                          SimulationVariant::damage_bias);
    if (!expected.ok() || !divergent.ok()) {
        return 3;
    }
    const HashObservation local{FrameNumber{1U},
                                hash_canonical(expected.value()), true};
    const HashObservation remote{FrameNumber{1U},
                                 hash_canonical(divergent.value()), true};
    DesyncTracker tracker{0xD35A7CU};
    const auto diagnostic = tracker.observe(local, remote, {inputs},
                                            expected.value());
    if (!diagnostic.has_value()) {
        return 4;
    }
    const auto json = canonical_json(diagnostic.value());
    if (!json.ok()) {
        return 5;
    }
    const auto output = option_value(arguments, "--out");
    if (output.empty()) {
        std::cout << json.value();
        return 0;
    }
    const auto path = std::filesystem::path{output};
    std::error_code error;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), error);
    }
    if (error || !write_text(path, json.value())) {
        return 6;
    }
    std::cout << "desync diagnostic written: " << path.string() << '\n';
    return 0;
}

auto required_u32(const std::span<const std::string> arguments,
                  const std::string_view option) -> Result<std::uint32_t> {
    const auto value = option_value(arguments, option);
    if (value.empty()) {
        return Result<std::uint32_t>::failure(
            Error{ErrorCode::invalid_argument, 0U, 0U, "required_option"});
    }
    return parse_u32(value, 0U);
}

auto relay_command(const std::span<const std::string> arguments) -> int {
    const auto relay_port = required_u32(arguments, "--relay-port");
    const auto peer_a_port = required_u32(arguments, "--peer-a-port");
    const auto peer_b_port = required_u32(arguments, "--peer-b-port");
    const auto maximum = required_u32(arguments, "--max-ms");
    const auto ready = option_value(arguments, "--ready");
    if (!relay_port.ok() || !peer_a_port.ok() || !peer_b_port.ok() ||
        !maximum.ok() || ready.empty() || relay_port.value() > 65'535U ||
        peer_a_port.value() > 65'535U || peer_b_port.value() > 65'535U) {
        return 2;
    }
    const auto result = run_relay(RelayConfig{
        static_cast<std::uint16_t>(relay_port.value()),
        static_cast<std::uint16_t>(peer_a_port.value()),
        static_cast<std::uint16_t>(peer_b_port.value()), ready,
        maximum.value()});
    if (!result.ok()) {
        std::ofstream diagnostic{ready + ".error",
                                 std::ios::binary | std::ios::trunc};
        diagnostic << "{\"error_code\":"
                   << static_cast<unsigned>(result.error().code)
                   << ",\"detail\":" << result.error().detail
                   << ",\"offset\":" << result.error().offset
                   << ",\"context\":\"" << result.error().context
                   << "\"}\n";
        return 20 + static_cast<int>(result.error().code);
    }
    return result.value();
}

auto peer_command(const std::span<const std::string> arguments) -> int {
    const auto id = option_value(arguments, "--id");
    const auto listen_port = required_u32(arguments, "--listen-port");
    const auto relay_port = required_u32(arguments, "--relay-port");
    const auto scenario_seed = required_u32(arguments, "--scenario-seed");
    const auto transport_seed = required_u32(arguments, "--transport-seed");
    const auto frames = required_u32(arguments, "--frames");
    const auto protocol = required_u32(arguments, "--protocol-version");
    const auto simulation = required_u32(arguments, "--simulation-version");
    const auto variant = option_value(arguments, "--variant");
    const auto handshake = required_u32(arguments, "--handshake-ms");
    const auto run_timeout = required_u32(arguments, "--run-ms");
    const auto report = option_value(arguments, "--report");
    const auto replay = option_value(arguments, "--replay");
    const auto diagnostic = option_value(arguments, "--diagnostic");
    if ((id != "A" && id != "B") || !listen_port.ok() ||
        !relay_port.ok() || !scenario_seed.ok() || !transport_seed.ok() ||
        !frames.ok() || !protocol.ok() || !simulation.ok() || !handshake.ok() ||
        !run_timeout.ok() || report.empty() || replay.empty() ||
        diagnostic.empty() ||
        (variant != "canonical" && variant != "damage-bias") ||
        listen_port.value() > 65'535U || relay_port.value() > 65'535U ||
        protocol.value() > 65'535U) {
        return 2;
    }
    PeerConfig config{};
    config.id = id == "A" ? PlayerId::a : PlayerId::b;
    config.listen_port = static_cast<std::uint16_t>(listen_port.value());
    config.relay_port = static_cast<std::uint16_t>(relay_port.value());
    config.scenario_seed = scenario_seed.value();
    config.transport_seed = transport_seed.value();
    config.frame_count = frames.value();
    config.protocol_version_override =
        static_cast<std::uint16_t>(protocol.value());
    config.simulation_version_override = simulation.value();
    config.simulation_variant = variant == "damage-bias"
                                    ? SimulationVariant::damage_bias
                                    : SimulationVariant::canonical;
    config.handshake_timeout_milliseconds = handshake.value();
    config.run_timeout_milliseconds = run_timeout.value();
    config.report_path = report;
    config.replay_path = replay;
    config.diagnostic_path = diagnostic;
    const auto result = run_peer(config);
    if (!result.ok()) {
        std::ofstream diagnostic_file{report + ".error",
                                      std::ios::binary | std::ios::trunc};
        diagnostic_file << "{\"error_code\":"
                        << static_cast<unsigned>(result.error().code)
                        << ",\"detail\":" << result.error().detail
                        << ",\"offset\":" << result.error().offset
                        << ",\"context\":\"" << result.error().context
                        << "\"}\n";
        return 40 + static_cast<int>(result.error().code);
    }
    return result.value();
}

auto udp_demo_command(const std::span<const std::string> arguments) -> int {
    const auto frames = parse_u32(option_value(arguments, "--frames"), 120U);
    const auto watchdog =
        parse_u32(option_value(arguments, "--watchdog-ms"), 5'000U);
    if (!frames.ok() || !watchdog.ok()) {
        return 2;
    }
    UdpDemoConfig config{};
    config.executable_path = std::filesystem::absolute(arguments[0]);
    const auto output = option_value(arguments, "--out");
    if (!output.empty()) {
        config.output_directory = output;
    }
    config.frame_count = frames.value();
    config.watchdog_milliseconds = watchdog.value();
    if (std::find(arguments.begin(), arguments.end(), "--inject-desync") !=
        arguments.end()) {
        config.scenario_seed = 1U;
        config.peer_b_variant = SimulationVariant::damage_bias;
    }
    const auto result = run_udp_demo(config);
    if (!result.ok()) {
        std::cerr << "udp demo failed with error code "
                  << static_cast<unsigned>(result.error().code)
                  << ", detail " << result.error().detail
                  << ", context " << result.error().context << '\n';
        return 6;
    }
    std::cout << "udp demo passed: relay pid " << result.value().relay_pid
              << ", peers " << result.value().peer_a_pid << '/'
              << result.value().peer_b_pid << ", confirmed frame "
              << result.value().confirmed_frame.value << ", hash "
              << result.value().final_hash_a << '\n';
    return 0;
}

}  // namespace

auto run_cli(const std::span<const std::string> arguments) -> int {
    if (arguments.size() < 2U || arguments[1] == "--help" ||
        arguments[1] == "help") {
        print_help();
        return arguments.size() < 2U ? 2 : 0;
    }
    if (arguments[1] == "simulate") {
        return simulate_command(arguments);
    }
    if (arguments[1] == "replay") {
        return replay_command(arguments);
    }
    if (arguments[1] == "verify") {
        return verify_command(arguments);
    }
    if (arguments[1] == "benchmark") {
        return benchmark_command(arguments);
    }
    if (arguments[1] == "desync-demo") {
        return desync_demo_command(arguments);
    }
    if (arguments[1] == "compare") {
        return compare_command(arguments);
    }
    if (arguments[1] == "udp-demo") {
        return udp_demo_command(arguments);
    }
    if (arguments[1] == "relay") {
        return relay_command(arguments);
    }
    if (arguments[1] == "peer") {
        return peer_command(arguments);
    }
    print_help();
    return 2;
}

}  // namespace rollback_lab
