#include <rollback_lab/netcode/session.hpp>

#include <algorithm>
#include <cstdint>
#include <utility>

namespace rollback_lab {
namespace {

auto same_predicted_value(const InputFrame& left,
                          const InputFrame& right) noexcept -> bool {
    return left.buttons == right.buttons;
}

}  // namespace

struct RollbackSession::Storage final {
    FrameRing<InputFrame, kHistoryCapacity> local_inputs;
    FrameRing<InputFrame, kHistoryCapacity> remote_inputs;
    FrameRing<InputFrame, kHistoryCapacity> used_remote_inputs;
    FrameRing<WorldState, kHistoryCapacity> snapshots;
    FrameRing<StateHash, kHistoryCapacity> hashes;
};

RollbackSession::RollbackSession(SessionConfig config)
    : config_(std::move(config)),
      state_(config_.initial_state),
      storage_(std::make_unique<Storage>()) {
    metrics_.confirmed_frame = state_.frame;
    static_cast<void>(storage_->hashes.put(state_.frame, hash_canonical(state_)));
}

RollbackSession::~RollbackSession() = default;
RollbackSession::RollbackSession(RollbackSession&&) noexcept = default;
auto RollbackSession::operator=(RollbackSession&&) noexcept
    -> RollbackSession& = default;

auto RollbackSession::remote_peer() const noexcept -> PlayerId {
    return config_.local_peer == PlayerId::a ? PlayerId::b : PlayerId::a;
}

auto RollbackSession::predicted_remote(const FrameNumber frame) const
    -> InputFrame {
    for (std::uint32_t offset = 1U; offset <= kHistoryCapacity; ++offset) {
        const auto previous = FrameNumber{
            static_cast<std::uint32_t>(frame.value - offset)};
        const auto known = storage_->remote_inputs.get(previous);
        if (known.ok()) {
            auto prediction = known.value();
            prediction.frame = frame;
            return prediction;
        }
    }
    return InputFrame{frame, remote_peer(), 0U, 0U};
}

auto RollbackSession::make_pair(const InputFrame& local,
                                const InputFrame& remote) const -> InputPair {
    return config_.local_peer == PlayerId::a ? InputPair{local, remote}
                                             : InputPair{remote, local};
}

void RollbackSession::mark_dirty(const FrameNumber frame) {
    if (!earliest_dirty_.has_value() ||
        frame_before(frame, earliest_dirty_.value())) {
        earliest_dirty_ = frame;
    }
}

void RollbackSession::update_confirmation() {
    auto boundary = metrics_.confirmed_frame;
    while (storage_->local_inputs.contains(boundary) &&
           storage_->remote_inputs.contains(boundary)) {
        boundary = next_frame(boundary);
    }
    metrics_.confirmed_frame = boundary;
}

auto RollbackSession::advance(InputFrame local) -> Result<AdvanceResult> {
    if (earliest_dirty_.has_value()) {
        const auto correction = flush_corrections();
        if (!correction.ok()) {
            return Result<AdvanceResult>::failure(correction.error());
        }
    }
    if (local.frame != state_.frame || local.player != config_.local_peer) {
        return Result<AdvanceResult>::failure(
            Error{ErrorCode::frame_mismatch, local.frame.value, 0U,
                  "session_advance"});
    }

    const auto snapshot_result = storage_->snapshots.put(state_.frame, state_);
    const auto local_result = storage_->local_inputs.put(local.frame, local);
    if (!snapshot_result.ok() || !local_result.ok()) {
        return Result<AdvanceResult>::failure(
            Error{ErrorCode::capacity_exceeded, local.frame.value, 0U,
                  "session_history"});
    }

    const auto actual = storage_->remote_inputs.get(state_.frame);
    const bool predicted = !actual.ok();
    const auto remote = actual.ok() ? actual.value() : predicted_remote(state_.frame);
    static_cast<void>(storage_->used_remote_inputs.put(state_.frame, remote));
    if (predicted) {
        ++metrics_.predicted_input_count;
    }

    const auto simulated = simulate_frame(
        state_, state_.frame, make_pair(local, remote), config_.variant);
    if (!simulated.ok()) {
        return Result<AdvanceResult>::failure(simulated.error());
    }
    const auto simulated_frame = state_.frame;
    state_ = simulated.value();
    const auto state_hash = hash_canonical(state_);
    static_cast<void>(storage_->hashes.put(state_.frame, state_hash));
    update_confirmation();
    return Result<AdvanceResult>::success(
        AdvanceResult{simulated_frame, predicted, state_hash});
}

auto RollbackSession::ingest_remote(InputFrame remote) -> Result<void> {
    if (remote.player != remote_peer()) {
        return Result<void>::failure(
            Error{ErrorCode::invalid_argument,
                  static_cast<std::uint64_t>(remote.player), 0U,
                  "remote_peer_id"});
    }

    const bool is_late = frame_before(remote.frame, state_.frame);
    if (is_late && frame_distance(remote.frame, state_.frame) >
                       config_.max_rollback_frames) {
        return Result<void>::failure(
            Error{ErrorCode::rollback_window_exceeded, remote.frame.value, 0U,
                  "remote_input"});
    }

    const auto existing = storage_->remote_inputs.get(remote.frame);
    const bool newly_observed = !existing.ok();
    static_cast<void>(storage_->remote_inputs.put(remote.frame, remote));
    if (is_late && newly_observed) {
        ++metrics_.late_input_count;
    }
    if (is_late) {
        const auto used = storage_->used_remote_inputs.get(remote.frame);
        if (used.ok() && !same_predicted_value(used.value(), remote)) {
            mark_dirty(remote.frame);
        }
    }
    update_confirmation();
    return Result<void>::success();
}

auto RollbackSession::flush_corrections() -> Result<CorrectionResult> {
    if (!earliest_dirty_.has_value()) {
        update_confirmation();
        return Result<CorrectionResult>::success(CorrectionResult{});
    }

    const auto dirty = earliest_dirty_.value();
    const auto depth = frame_distance(dirty, state_.frame);
    if (depth > config_.max_rollback_frames) {
        return Result<CorrectionResult>::failure(
            Error{ErrorCode::rollback_window_exceeded, dirty.value, 0U,
                  "flush_corrections"});
    }
    const auto snapshot = storage_->snapshots.get(dirty);
    if (!snapshot.ok()) {
        return Result<CorrectionResult>::failure(snapshot.error());
    }

    state_ = snapshot.value();
    for (std::uint32_t offset = 0U; offset < depth; ++offset) {
        const auto frame = FrameNumber{
            static_cast<std::uint32_t>(dirty.value + offset)};
        const auto local = storage_->local_inputs.get(frame);
        if (!local.ok()) {
            return Result<CorrectionResult>::failure(local.error());
        }
        static_cast<void>(storage_->snapshots.put(frame, state_));
        const auto actual = storage_->remote_inputs.get(frame);
        const auto remote = actual.ok() ? actual.value() : predicted_remote(frame);
        static_cast<void>(storage_->used_remote_inputs.put(frame, remote));
        const auto simulated = simulate_frame(
            state_, frame, make_pair(local.value(), remote), config_.variant);
        if (!simulated.ok()) {
            return Result<CorrectionResult>::failure(simulated.error());
        }
        state_ = simulated.value();
        static_cast<void>(storage_->hashes.put(state_.frame, hash_canonical(state_)));
    }

    earliest_dirty_.reset();
    ++metrics_.rollback_count;
    metrics_.total_resimulated_frames += depth;
    metrics_.maximum_rollback_depth =
        std::max(metrics_.maximum_rollback_depth, depth);
    update_confirmation();
    return Result<CorrectionResult>::success(
        CorrectionResult{true, dirty, depth});
}

auto RollbackSession::report() const -> SessionReport {
    return SessionReport{config_.local_peer, state_, metrics_,
                         hash_canonical(state_)};
}

auto RollbackSession::confirmed_input(const PlayerId player,
                                      const FrameNumber frame) const
    -> Result<InputFrame> {
    if (!frame_before(frame, metrics_.confirmed_frame)) {
        return Result<InputFrame>::failure(
            Error{ErrorCode::stale_frame, frame.value, 0U,
                  "unconfirmed_input"});
    }
    return player == config_.local_peer ? storage_->local_inputs.get(frame)
                                        : storage_->remote_inputs.get(frame);
}

auto RollbackSession::hash_at(const FrameNumber boundary) const
    -> Result<StateHash> {
    if (frame_before(metrics_.confirmed_frame, boundary)) {
        return Result<StateHash>::failure(
            Error{ErrorCode::stale_frame, boundary.value, 0U,
                  "unconfirmed_hash"});
    }
    return storage_->hashes.get(boundary);
}

}  // namespace rollback_lab
