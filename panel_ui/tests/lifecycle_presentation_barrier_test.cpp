#include "panel_ui/lifecycle_presentation_barrier.hpp"

#include <future>
#include <iostream>
#include <utility>

namespace house_panel::panel_ui {
namespace {

namespace protocol = bhp::panel_mode_protocol;

#define TEST_REQUIRE(condition)                                                                    \
	do {                                                                                       \
		if (!(condition)) {                                                                \
			std::cerr << __func__ << ": requirement failed: " #condition << '\n';      \
			return false;                                                              \
		}                                                                                  \
	} while (false)

constexpr LifecycleRenderCommit first_committed{7, protocol::PresentationResult::Committed};
constexpr LifecycleRenderCommit next_committed{11, protocol::PresentationResult::Committed};
constexpr LifecycleRenderCommit first_failed{7, protocol::PresentationResult::Failed};

bool render_promise_completion_keeps_raster_held_until_acknowledgement_send_completes()
{
	LifecyclePresentationBarrier barrier;
	std::promise<LifecycleRenderCommit> completion;
	auto rendered = completion.get_future();
	TEST_REQUIRE(barrier.hold_until_exchange_completed(first_committed.render_generation));
	completion.set_value(first_committed);
	const auto render_commit = rendered.get();
	TEST_REQUIRE(!barrier.scene_changes_allowed());
	TEST_REQUIRE(!barrier.input_ready(render_commit.render_generation));
	TEST_REQUIRE(barrier.exchange_completed_generation() == 0);
	TEST_REQUIRE(barrier.acknowledged_generation() == 0);
	TEST_REQUIRE(barrier.complete_exchange(render_commit));
	TEST_REQUIRE(barrier.scene_changes_allowed());
	TEST_REQUIRE(barrier.input_ready(render_commit.render_generation));
	TEST_REQUIRE(barrier.exchange_completed_generation() == render_commit.render_generation);
	return true;
}

bool only_the_matching_generation_can_release_the_raster_or_enable_input()
{
	LifecyclePresentationBarrier barrier;
	TEST_REQUIRE(barrier.hold_until_exchange_completed(first_committed.render_generation));
	TEST_REQUIRE(!barrier.complete_exchange(next_committed));
	TEST_REQUIRE(!barrier.scene_changes_allowed());
	TEST_REQUIRE(!barrier.input_ready(first_committed.render_generation));
	TEST_REQUIRE(barrier.complete_exchange(first_committed));
	TEST_REQUIRE(barrier.input_ready(first_committed.render_generation));
	TEST_REQUIRE(!barrier.input_ready(next_committed.render_generation));
	TEST_REQUIRE(!barrier.complete_exchange(first_failed));
	TEST_REQUIRE(barrier.input_ready(first_committed.render_generation));
	return true;
}

bool failed_acknowledgement_completes_exchange_without_input_readiness()
{
	LifecyclePresentationBarrier barrier;
	TEST_REQUIRE(barrier.hold_until_exchange_completed(first_failed.render_generation));
	TEST_REQUIRE(barrier.complete_exchange(first_failed));
	TEST_REQUIRE(barrier.scene_changes_allowed());
	TEST_REQUIRE(barrier.exchange_completed_generation() == first_failed.render_generation);
	TEST_REQUIRE(barrier.acknowledged_generation() == 0);
	TEST_REQUIRE(!barrier.input_ready(first_failed.render_generation));
	TEST_REQUIRE(barrier.hold_until_exchange_completed(next_committed.render_generation));
	TEST_REQUIRE(barrier.complete_exchange(next_committed));
	TEST_REQUIRE(barrier.input_ready(next_committed.render_generation));
	return true;
}

bool a_late_old_completion_cannot_unlock_a_new_generation()
{
	LifecyclePresentationBarrier barrier;
	TEST_REQUIRE(barrier.hold_until_exchange_completed(first_committed.render_generation));
	barrier.disconnect();
	TEST_REQUIRE(barrier.hold_until_exchange_completed(next_committed.render_generation));
	TEST_REQUIRE(!barrier.complete_exchange(first_committed));
	TEST_REQUIRE(!barrier.scene_changes_allowed());
	TEST_REQUIRE(barrier.acknowledged_generation() == 0);
	TEST_REQUIRE(barrier.exchange_completed_generation() == 0);
	TEST_REQUIRE(barrier.complete_exchange(next_committed));
	TEST_REQUIRE(barrier.input_ready(next_committed.render_generation));
	return true;
}

bool disconnect_cancels_the_hold_and_clears_existing_readiness()
{
	LifecyclePresentationBarrier barrier;
	TEST_REQUIRE(barrier.hold_until_exchange_completed(first_committed.render_generation));
	TEST_REQUIRE(barrier.complete_exchange(first_committed));
	barrier.disconnect();
	TEST_REQUIRE(barrier.scene_changes_allowed());
	TEST_REQUIRE(!barrier.input_ready(first_committed.render_generation));
	TEST_REQUIRE(barrier.acknowledged_generation() == 0);
	TEST_REQUIRE(barrier.exchange_completed_generation() == 0);
	TEST_REQUIRE(!barrier.hold_until_exchange_completed(first_committed.render_generation));
	TEST_REQUIRE(barrier.hold_until_exchange_completed(next_committed.render_generation));
	barrier.disconnect();
	TEST_REQUIRE(barrier.scene_changes_allowed());
	TEST_REQUIRE(!barrier.complete_exchange(next_committed));
	return true;
}

bool display_commit_failure_permanently_stops_scene_changes_and_readiness()
{
	LifecyclePresentationBarrier barrier;
	TEST_REQUIRE(barrier.hold_until_exchange_completed(first_committed.render_generation));
	TEST_REQUIRE(barrier.complete_exchange(first_committed));
	barrier.fail_display_commit();
	TEST_REQUIRE(barrier.display_failed());
	TEST_REQUIRE(!barrier.scene_changes_allowed());
	TEST_REQUIRE(!barrier.input_ready(first_committed.render_generation));
	TEST_REQUIRE(barrier.acknowledged_generation() == 0);
	TEST_REQUIRE(barrier.exchange_completed_generation() == 0);
	TEST_REQUIRE(!barrier.hold_until_exchange_completed(next_committed.render_generation));
	TEST_REQUIRE(!barrier.complete_exchange(next_committed));
	barrier.disconnect();
	TEST_REQUIRE(!barrier.scene_changes_allowed());
	TEST_REQUIRE(barrier.display_failed());
	return true;
}

bool stopping_during_a_held_frame_never_waits_for_a_late_completion()
{
	LifecyclePresentationBarrier barrier;
	TEST_REQUIRE(barrier.hold_until_exchange_completed(first_committed.render_generation));
	barrier.stop();
	TEST_REQUIRE(!barrier.scene_changes_allowed());
	TEST_REQUIRE(!barrier.input_ready(first_committed.render_generation));
	TEST_REQUIRE(!barrier.complete_exchange(first_committed));
	TEST_REQUIRE(!barrier.hold_until_exchange_completed(next_committed.render_generation));
	TEST_REQUIRE(barrier.acknowledged_generation() == 0);
	return true;
}

bool a_new_hold_clears_old_readiness_and_rejects_replacement_until_completion()
{
	LifecyclePresentationBarrier barrier;
	TEST_REQUIRE(!barrier.hold_until_exchange_completed(0));
	TEST_REQUIRE(!barrier.input_ready(0));
	TEST_REQUIRE(barrier.hold_until_exchange_completed(first_committed.render_generation));
	TEST_REQUIRE(!barrier.hold_until_exchange_completed(next_committed.render_generation));
	TEST_REQUIRE(barrier.complete_exchange(first_committed));
	TEST_REQUIRE(barrier.hold_until_exchange_completed(next_committed.render_generation));
	TEST_REQUIRE(!barrier.input_ready(first_committed.render_generation));
	TEST_REQUIRE(!barrier.scene_changes_allowed());
	TEST_REQUIRE(barrier.exchange_completed_generation() == first_committed.render_generation);
	TEST_REQUIRE(barrier.acknowledged_generation() == 0);
	return true;
}

} // namespace
} // namespace house_panel::panel_ui

int main()
{
	using namespace house_panel::panel_ui;
	const std::pair<const char *, bool (*)()> tests[]{
		{"render promise retains raster", render_promise_completion_keeps_raster_held_until_acknowledgement_send_completes},
		{"exact completion generation", only_the_matching_generation_can_release_the_raster_or_enable_input},
		{"failed acknowledgement is not readiness", failed_acknowledgement_completes_exchange_without_input_readiness},
		{"late old completion", a_late_old_completion_cannot_unlock_a_new_generation},
		{"disconnect clears hold and readiness", disconnect_cancels_the_hold_and_clears_existing_readiness},
		{"display failure stops runtime", display_commit_failure_permanently_stops_scene_changes_and_readiness},
		{"stop cancels pending completion", stopping_during_a_held_frame_never_waits_for_a_late_completion},
		{"new hold clears old readiness", a_new_hold_clears_old_readiness_and_rejects_replacement_until_completion},
	};
	for (const auto &test : tests) {
		if (!test.second()) {
			std::cerr << "FAIL: " << test.first << '\n';
			return 1;
		}
	}
	std::cout << "PASS: lifecycle presentation barrier (8 cases)\n";
	return 0;
}
