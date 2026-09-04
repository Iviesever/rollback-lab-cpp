#pragma once

#include <rollback_lab/netcode/session.hpp>
#include <rollback_lab/report/scenario_runner.hpp>

#include <memory>
#include <optional>

namespace rollback_lab {

// A copied presentation event captured at the actual correction boundary.
struct LiveCorrection final {
    CorrectionResult result{};
    WorldState before{};
    WorldState after{};
};

// Single-thread-affine driver borrowing two unused, independent A/B sessions.
// Sessions must outlive the driver and must not be mutated externally while
// borrowed. Only copied report/query data may be inspected between steps.
class LiveScenario final {
  public:
    [[nodiscard]] static auto create(const ScenarioRunConfig &config, RollbackSession &peer_a,
                                     RollbackSession &peer_b)
        -> Result<std::unique_ptr<LiveScenario>>;
    ~LiveScenario();
    LiveScenario(const LiveScenario &) = delete;
    auto operator=(const LiveScenario &) -> LiveScenario & = delete;

    // One logical transport tick; a true value means the bounded tail/drain and
    // replay verification are complete. Repeated calls after completion are no-op.
    [[nodiscard]] auto step(std::optional<std::uint8_t> local_buttons = {}) -> Result<bool>;
    [[nodiscard]] auto logical_tick() const noexcept -> std::uint32_t;
    [[nodiscard]] auto artifacts() const noexcept -> const ScenarioArtifacts &;
    [[nodiscard]] auto correction(PlayerId peer) const noexcept -> const LiveCorrection &;

  private:
    struct Storage;
    explicit LiveScenario(std::unique_ptr<Storage> storage);
    std::unique_ptr<Storage> storage_;
};

} // namespace rollback_lab
