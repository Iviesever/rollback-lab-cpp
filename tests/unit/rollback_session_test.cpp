#include "test_framework.hpp"

#include <rollback_lab/netcode/frame_ring.hpp>
#include <rollback_lab/netcode/session.hpp>
#include <rollback_lab/simulation/scripted_input.hpp>

#include <cstdint>
#include <deque>
#include <type_traits>

namespace {

using namespace rollback_lab;

auto input(const FrameNumber frame,
           const PlayerId player,
           const std::uint8_t buttons = 0U) -> InputFrame {
    return InputFrame{frame, player, frame.value, buttons};
}

void require_advance(RollbackSession& session, const InputFrame local) {
    const auto result = session.advance(local);
    RL_REQUIRE(result.ok());
}

}  // namespace

RL_TEST(frame_ring_rejects_missing_slot_and_survives_wrap) {
    FrameRing<std::uint32_t, 256U> ring;
    RL_CHECK(!ring.get(FrameNumber{0U}).ok());
    RL_REQUIRE(ring.put(FrameNumber{0U}, 10U).ok());
    RL_CHECK(ring.get(FrameNumber{0U}).value() == 10U);
    RL_REQUIRE(ring.put(FrameNumber{256U}, 20U).ok());
    RL_CHECK(!ring.get(FrameNumber{0U}).ok());
    RL_CHECK(ring.get(FrameNumber{256U}).value() == 20U);
}

RL_TEST(zero_latency_uses_no_prediction_and_no_rollback) {
    RollbackSession session{SessionConfig{PlayerId::a}};
    for (std::uint32_t frame = 0; frame < 60U; ++frame) {
        const auto number = FrameNumber{frame};
        RL_REQUIRE(session.ingest_remote(input(number, PlayerId::b)).ok());
        require_advance(session, input(number, PlayerId::a));
    }
    const auto report = session.report();
    RL_CHECK(report.metrics.rollback_count == 0U);
    RL_CHECK(report.metrics.predicted_input_count == 0U);
    RL_CHECK(report.metrics.confirmed_frame == FrameNumber{60U});
}

RL_TEST(late_input_matching_prediction_does_not_rollback) {
    RollbackSession session{SessionConfig{PlayerId::a}};
    for (std::uint32_t frame = 0; frame < 10U; ++frame) {
        require_advance(session, input(FrameNumber{frame}, PlayerId::a));
    }
    for (std::uint32_t frame = 0; frame < 10U; ++frame) {
        RL_REQUIRE(session.ingest_remote(input(FrameNumber{frame}, PlayerId::b)).ok());
    }
    const auto correction = session.flush_corrections();
    RL_REQUIRE(correction.ok());
    RL_CHECK(!correction.value().performed);
    const auto report = session.report();
    RL_CHECK(report.metrics.rollback_count == 0U);
    RL_CHECK(report.metrics.predicted_input_count == 10U);
    RL_CHECK(report.metrics.late_input_count == 10U);
    RL_CHECK(report.metrics.confirmed_frame == FrameNumber{10U});
}

RL_TEST(late_mismatches_coalesce_at_earliest_dirty_boundary) {
    RollbackSession session{SessionConfig{PlayerId::a}};
    for (std::uint32_t frame = 0; frame < 12U; ++frame) {
        require_advance(session, input(FrameNumber{frame}, PlayerId::a));
    }
    RL_REQUIRE(session.ingest_remote(input(FrameNumber{7U}, PlayerId::b,
                                           button_mask(Button::right))).ok());
    RL_REQUIRE(session.ingest_remote(input(FrameNumber{5U}, PlayerId::b,
                                           button_mask(Button::left))).ok());
    const auto correction = session.flush_corrections();
    RL_REQUIRE(correction.ok());
    RL_CHECK(correction.value().performed);
    RL_CHECK(correction.value().rollback_from == FrameNumber{5U});
    RL_CHECK(correction.value().resimulated_frames == 7U);
    const auto report = session.report();
    RL_CHECK(report.metrics.rollback_count == 1U);
    RL_CHECK(report.metrics.total_resimulated_frames == 7U);
    RL_CHECK(report.metrics.maximum_rollback_depth == 7U);
}

RL_TEST(input_beyond_window_fails_closed_but_boundary_is_accepted) {
    RollbackSession boundary{SessionConfig{PlayerId::a}};
    for (std::uint32_t frame = 0; frame < 120U; ++frame) {
        require_advance(boundary, input(FrameNumber{frame}, PlayerId::a));
    }
    RL_CHECK(boundary.ingest_remote(input(FrameNumber{0U}, PlayerId::b,
                                          button_mask(Button::left))).ok());

    RollbackSession stale{SessionConfig{PlayerId::a}};
    for (std::uint32_t frame = 0; frame < 121U; ++frame) {
        require_advance(stale, input(FrameNumber{frame}, PlayerId::a));
    }
    const auto result = stale.ingest_remote(input(FrameNumber{0U}, PlayerId::b,
                                                   button_mask(Button::left)));
    RL_CHECK(!result.ok());
    RL_CHECK(result.error().code == ErrorCode::rollback_window_exceeded);
}

RL_TEST(resimulation_replaces_projectile_effect_instead_of_double_applying) {
    RollbackSession session{SessionConfig{PlayerId::a}};
    for (std::uint32_t frame = 0; frame < 30U; ++frame) {
        const std::uint8_t buttons =
            frame == 2U ? button_mask(Button::attack) : std::uint8_t{0U};
        require_advance(session, input(FrameNumber{frame}, PlayerId::a, buttons));
    }
    const auto before = session.report();
    RL_REQUIRE(session.ingest_remote(input(FrameNumber{1U}, PlayerId::b,
                                           button_mask(Button::up))).ok());
    const auto correction = session.flush_corrections();
    RL_REQUIRE(correction.ok());
    RL_CHECK(correction.value().performed);
    const auto after = session.report();
    RL_CHECK(after.state.next_projectile_id == before.state.next_projectile_id);
}

RL_TEST(two_independent_sessions_converge_after_all_inputs_arrive) {
    RollbackSession peer_a{SessionConfig{PlayerId::a}};
    RollbackSession peer_b{SessionConfig{PlayerId::b}};
    std::deque<InputFrame> delayed_a;
    std::deque<InputFrame> delayed_b;

    for (std::uint32_t frame = 0; frame < 180U; ++frame) {
        const auto number = FrameNumber{frame};
        const auto actual_a = scripted_input(7U, number, PlayerId::a);
        const auto actual_b = scripted_input(7U, number, PlayerId::b);
        delayed_a.push_back(actual_a);
        delayed_b.push_back(actual_b);
        if (delayed_a.size() > 4U) {
            RL_REQUIRE(peer_b.ingest_remote(delayed_a.front()).ok());
            delayed_a.pop_front();
        }
        if (delayed_b.size() > 7U) {
            RL_REQUIRE(peer_a.ingest_remote(delayed_b.front()).ok());
            delayed_b.pop_front();
        }
        require_advance(peer_a, actual_a);
        require_advance(peer_b, actual_b);
        RL_REQUIRE(peer_a.flush_corrections().ok());
        RL_REQUIRE(peer_b.flush_corrections().ok());
    }

    while (!delayed_a.empty()) {
        RL_REQUIRE(peer_b.ingest_remote(delayed_a.front()).ok());
        delayed_a.pop_front();
    }
    while (!delayed_b.empty()) {
        RL_REQUIRE(peer_a.ingest_remote(delayed_b.front()).ok());
        delayed_b.pop_front();
    }
    RL_REQUIRE(peer_a.flush_corrections().ok());
    RL_REQUIRE(peer_b.flush_corrections().ok());

    const auto report_a = peer_a.report();
    const auto report_b = peer_b.report();
    RL_CHECK(report_a.metrics.confirmed_frame == FrameNumber{180U});
    RL_CHECK(report_b.metrics.confirmed_frame == FrameNumber{180U});
    RL_CHECK(report_a.state == report_b.state);
    RL_CHECK(report_a.final_hash == report_b.final_hash);
    RL_CHECK(report_a.metrics.rollback_count > 0U);
    RL_CHECK(report_b.metrics.rollback_count > 0U);
}

RL_TEST(rollback_session_is_move_only_to_prevent_accidental_peer_aliasing) {
    static_assert(!std::is_copy_constructible_v<RollbackSession>);
    static_assert(!std::is_copy_assignable_v<RollbackSession>);
    static_assert(std::is_move_constructible_v<RollbackSession>);
}
