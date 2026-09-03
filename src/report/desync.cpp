#include <rollback_lab/report/desync.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace rollback_lab {

DesyncTracker::DesyncTracker(const std::uint64_t scenario_seed)
    : scenario_seed_(scenario_seed) {}

auto DesyncTracker::observe(const HashObservation& local,
                            const HashObservation& remote,
                            std::vector<InputPair> recent_inputs,
                            const WorldState& local_state)
    -> std::optional<DesyncDiagnostic> {
    if (!local.confirmed || !remote.confirmed || local.frame != remote.frame ||
        local.hash == remote.hash) {
        return diagnostic_;
    }
    if (diagnostic_.has_value() &&
        !frame_before(local.frame, diagnostic_->earliest_divergent_frame)) {
        return diagnostic_;
    }
    if (recent_inputs.size() > 32U) {
        recent_inputs.erase(recent_inputs.begin(),
                            recent_inputs.end() - 32);
    }
    const auto active = static_cast<std::uint32_t>(std::count_if(
        local_state.projectiles.begin(), local_state.projectiles.end(),
        [](const ProjectileState& projectile) { return projectile.active; }));
    DesyncDiagnostic diagnostic{};
    diagnostic.earliest_divergent_frame = local.frame;
    diagnostic.local_hash = local.hash;
    diagnostic.remote_hash = remote.hash;
    diagnostic.recent_inputs = std::move(recent_inputs);
    for (std::size_t index = 0U; index < local_state.players.size(); ++index) {
        const auto& player = local_state.players[index];
        diagnostic.players[index] = PlayerStateSummary{
            player.id, player.x, player.y, player.hp, player.score};
    }
    diagnostic.active_projectiles = active;
    diagnostic.scenario_seed = scenario_seed_;
    diagnostic_ = std::move(diagnostic);
    return diagnostic_;
}

auto DesyncTracker::diagnostic() const
    -> const std::optional<DesyncDiagnostic>& {
    return diagnostic_;
}

}  // namespace rollback_lab

