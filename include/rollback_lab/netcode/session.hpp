#pragma once

#include <rollback_lab/core/error.hpp>
#include <rollback_lab/core/hash.hpp>
#include <rollback_lab/netcode/frame_ring.hpp>
#include <rollback_lab/netcode/metrics.hpp>
#include <rollback_lab/simulation/simulation.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

namespace rollback_lab {

inline constexpr std::size_t kHistoryCapacity = 256U;
inline constexpr std::uint32_t kDefaultMaxRollbackFrames = 120U;

struct SessionConfig final {
    PlayerId local_peer{PlayerId::a};
    WorldState initial_state{make_initial_world()};
    std::uint32_t max_rollback_frames{kDefaultMaxRollbackFrames};
    SimulationVariant variant{SimulationVariant::canonical};
};

struct AdvanceResult final {
    FrameNumber simulated_frame{};
    bool predicted_remote{};
    StateHash state_hash{};
};

struct CorrectionResult final {
    bool performed{};
    FrameNumber rollback_from{};
    std::uint32_t resimulated_frames{};
};

struct SessionReport final {
    PlayerId local_peer{PlayerId::a};
    WorldState state{};
    RollbackMetrics metrics{};
    StateHash final_hash{};
};

class RollbackSession final {
public:
    explicit RollbackSession(SessionConfig config);
    ~RollbackSession();

    RollbackSession(const RollbackSession&) = delete;
    auto operator=(const RollbackSession&) -> RollbackSession& = delete;
    RollbackSession(RollbackSession&&) noexcept;
    auto operator=(RollbackSession&&) noexcept -> RollbackSession&;

    [[nodiscard]] auto advance(InputFrame local) -> Result<AdvanceResult>;
    [[nodiscard]] auto ingest_remote(InputFrame remote) -> Result<void>;
    [[nodiscard]] auto flush_corrections() -> Result<CorrectionResult>;
    [[nodiscard]] auto report() const -> SessionReport;
    [[nodiscard]] auto confirmed_input(PlayerId player, FrameNumber frame) const
        -> Result<InputFrame>;
    [[nodiscard]] auto hash_at(FrameNumber boundary) const -> Result<StateHash>;
    [[nodiscard]] auto state_at(FrameNumber boundary) const -> Result<WorldState>;

private:
    [[nodiscard]] auto remote_peer() const noexcept -> PlayerId;
    [[nodiscard]] auto predicted_remote(FrameNumber frame) const -> InputFrame;
    [[nodiscard]] auto make_pair(const InputFrame& local,
                                 const InputFrame& remote) const -> InputPair;
    void mark_dirty(FrameNumber frame);
    void update_confirmation();

    struct Storage;

    SessionConfig config_;
    WorldState state_;
    std::unique_ptr<Storage> storage_;
    std::optional<FrameNumber> earliest_dirty_;
    RollbackMetrics metrics_{};
};

}  // namespace rollback_lab
