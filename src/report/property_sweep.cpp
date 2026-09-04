#include <rollback_lab/report/property_sweep.hpp>

#include <rollback_lab/core/pcg32.hpp>
#include <rollback_lab/replay/replay.hpp>
#include <rollback_lab/report/run_report.hpp>
#include <rollback_lab/report/scenario_runner.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <sstream>

namespace rollback_lab {
namespace {

constexpr std::array<std::uint32_t, 4U> loss_rates{0U, 1U, 5U, 20U};
constexpr std::array<std::uint32_t, 7U> edge_frame_counts{
    1U, 32U, 120U, 121U, 255U, 256U, 257U};
constexpr StateHash digest_offset = 14695981039346656037ULL;
constexpr StateHash digest_prime = 1099511628211ULL;

auto mix_digest(StateHash digest, const std::uint64_t value) -> StateHash {
    for (std::uint32_t shift = 0U; shift < 64U; shift += 8U) {
        digest ^= static_cast<std::uint8_t>((value >> shift) & 0xFFU);
        digest *= digest_prime;
    }
    return digest;
}

struct SweepCase final {
    ScenarioRunConfig scenario;
    std::optional<ErrorCode> expected_failure;
};

auto scenario_for(const std::uint32_t seed_index,
                  const std::uint32_t offset) -> SweepCase {
    Pcg32 random{0x9E3779B97F4A7C15ULL ^ seed_index,
                 0xD1B54A32D192ED03ULL + seed_index};
    ScenarioRunConfig config{};
    config.scenario_seed =
        (static_cast<std::uint64_t>(random.next_u32()) << 32U) |
        random.next_u32();
    config.transport_seed =
        (static_cast<std::uint64_t>(random.next_u32()) << 32U) |
        random.next_u32();
    config.frame_count = offset < edge_frame_counts.size()
                             ? edge_frame_counts[offset]
                             : 40U + random.bounded(57U);
    config.capture_trace = false;
    config.tail_redundancy_ticks = 64U;
    config.transport.seed = config.transport_seed;
    config.transport.base_latency_ticks = random.bounded(7U);
    config.transport.jitter_ticks = random.bounded(4U);
    config.transport.loss_percent = loss_rates[random.bounded(4U)];
    config.transport.reorder_percent = random.bounded(21U);
    config.transport.duplicate_percent = random.bounded(11U);
    config.transport.burst_loss_percent = random.bounded(4U);
    config.transport.max_queue_packets = 512U;
    config.transport.max_queue_bytes = 512U * 1'200U;
    config.transport.bandwidth_bytes_per_tick = 1U << 20U;
    config.transport.max_packet_age_ticks = 128U;
    std::optional<ErrorCode> expected;
    if (offset >= edge_frame_counts.size() && offset % 50U == 7U) {
        config.transport.base_latency_ticks = 20U;
        config.transport.jitter_ticks = 0U;
        config.transport.loss_percent = 0U;
        config.transport.reorder_percent = 0U;
        config.transport.duplicate_percent = 0U;
        config.transport.burst_loss_percent = 0U;
        config.transport.max_queue_packets = 1U;
        config.transport.max_queue_bytes = 1'200U;
        expected = ErrorCode::queue_overflow;
    } else if (offset >= edge_frame_counts.size() && offset % 50U == 8U) {
        config.transport.base_latency_ticks = 10U;
        config.transport.jitter_ticks = 0U;
        config.transport.loss_percent = 0U;
        config.transport.reorder_percent = 0U;
        config.transport.duplicate_percent = 0U;
        config.transport.burst_loss_percent = 0U;
        config.transport.max_packet_age_ticks = 0U;
        expected = ErrorCode::timeout;
    }
    return SweepCase{config, expected};
}

auto validate_success(const ScenarioArtifacts& artifacts,
                      const ScenarioRunConfig& config) -> bool {
    const auto replay = verify_replay(artifacts.replay);
    return artifacts.report.success && artifacts.report.replay_verified &&
           replay.ok() &&
           artifacts.report.confirmed_frame == FrameNumber{config.frame_count} &&
           artifacts.report.final_hash_a == artifacts.report.final_hash_b &&
           artifacts.report.final_hash_a ==
               artifacts.replay.expected_final_hash &&
           artifacts.trace.frames.empty() && artifacts.trace.packets.empty() &&
           artifacts.trace.rollbacks.empty();
}

}  // namespace

auto run_property_sweep(const PropertySweepConfig& config)
    -> Result<PropertySweepResult> {
    if (config.seed_count == 0U || config.seed_count > 100'000U ||
        config.repeat_identity_samples > config.seed_count) {
        return Result<PropertySweepResult>::failure(
            Error{ErrorCode::invalid_argument, config.seed_count, 0U,
                  "property_sweep_config"});
    }

    PropertySweepResult result{};
    result.start_seed = config.start_seed;
    result.total_seeds = config.seed_count;
    result.edge_frame_cases =
        std::min(config.seed_count,
                 static_cast<std::uint32_t>(edge_frame_counts.size()));
    StateHash digest = digest_offset;
    for (std::uint32_t offset = 0U; offset < config.seed_count; ++offset) {
        const auto seed = static_cast<std::uint32_t>(config.start_seed + offset);
        const auto sweep_case = scenario_for(seed, offset);
        const auto& scenario = sweep_case.scenario;
        const auto loss_index = static_cast<std::size_t>(
            std::find(loss_rates.begin(), loss_rates.end(),
                      scenario.transport.loss_percent) - loss_rates.begin());
        ++result.loss_rate_scenarios[loss_index];

        const auto first = run_seeded_scenario(scenario);
        digest = mix_digest(digest, seed);
        if (sweep_case.expected_failure.has_value()) {
            if (first.ok() ||
                first.error().code != sweep_case.expected_failure.value()) {
                ++result.unbounded_failures;
                return Result<PropertySweepResult>::failure(
                    Error{first.ok() ? ErrorCode::invalid_argument
                                     : first.error().code,
                          seed, first.ok() ? 0U : first.error().offset,
                          "property_expected_failure"});
            }
            ++result.declared_failures;
            if (first.error().code == ErrorCode::queue_overflow) {
                ++result.queue_overflow_failures;
            } else {
                ++result.timeout_failures;
            }
            digest = mix_digest(digest,
                                static_cast<std::uint64_t>(first.error().code));
            const auto repeated = run_seeded_scenario(scenario);
            ++result.repeated_identity_samples;
            if (repeated.ok() || repeated.error().code != first.error().code ||
                repeated.error().detail != first.error().detail) {
                ++result.identity_mismatches;
                return Result<PropertySweepResult>::failure(
                    Error{ErrorCode::replay_mismatch, seed, 0U,
                          "property_failure_repeat"});
            }
            continue;
        }
        if (!first.ok()) {
            ++result.unbounded_failures;
            return Result<PropertySweepResult>::failure(
                Error{first.error().code, seed, first.error().offset,
                      "property_unexpected_failure"});
        }
        if (!validate_success(first.value(), scenario)) {
            ++result.unbounded_failures;
            return Result<PropertySweepResult>::failure(
                Error{ErrorCode::replay_mismatch, seed, 0U,
                      "property_success_invariant"});
        }
        ++result.successful_seeds;
        const auto identity = report_identity(first.value().report);
        digest = mix_digest(digest, identity);

        if (offset < config.repeat_identity_samples) {
            const auto repeated = run_seeded_scenario(scenario);
            ++result.repeated_identity_samples;
            if (!repeated.ok() ||
                report_identity(repeated.value().report) != identity ||
                repeated.value().replay != first.value().replay) {
                ++result.identity_mismatches;
                return Result<PropertySweepResult>::failure(
                    Error{ErrorCode::replay_mismatch, seed, 0U,
                          "property_identity_repeat"});
            }
        }
    }
    result.identity_digest = digest;
    return Result<PropertySweepResult>::success(result);
}

auto canonical_json(const PropertySweepResult& result) -> std::string {
    std::ostringstream output;
    output << "{\n"
           << "  \"pcg32_version\":" << Pcg32::algorithm_version << ",\n"
           << "  \"start_seed\":" << result.start_seed << ",\n"
           << "  \"total_seeds\":" << result.total_seeds << ",\n"
           << "  \"successful_seeds\":" << result.successful_seeds << ",\n"
           << "  \"declared_failures\":" << result.declared_failures << ",\n"
           << "  \"repeated_identity_samples\":"
           << result.repeated_identity_samples << ",\n"
           << "  \"identity_mismatches\":" << result.identity_mismatches
           << ",\n"
           << "  \"crashes\":" << result.crashes << ",\n"
           << "  \"deadlocks\":" << result.deadlocks << ",\n"
           << "  \"unbounded_failures\":" << result.unbounded_failures
           << ",\n"
           << "  \"edge_frame_cases\":" << result.edge_frame_cases << ",\n"
           << "  \"queue_overflow_failures\":"
           << result.queue_overflow_failures << ",\n"
           << "  \"timeout_failures\":" << result.timeout_failures << ",\n"
           << "  \"loss_rate_scenarios\":{\"0\":"
           << result.loss_rate_scenarios[0] << ",\"1\":"
           << result.loss_rate_scenarios[1] << ",\"5\":"
           << result.loss_rate_scenarios[2] << ",\"20\":"
           << result.loss_rate_scenarios[3] << "},\n"
           << "  \"identity_digest\":" << result.identity_digest << ",\n"
           << "  \"full_sweep_repeated\":"
           << (result.full_sweep_repeated ? "true" : "false") << ",\n"
           << "  \"success\":"
           << (result.successful_seeds + result.declared_failures ==
                       result.total_seeds &&
                   result.identity_mismatches == 0U && result.crashes == 0U &&
                   result.deadlocks == 0U && result.unbounded_failures == 0U
               ? "true"
               : "false")
           << "\n}\n";
    return output.str();
}

}  // namespace rollback_lab
