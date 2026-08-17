#include "panel_ui/shell_controller.hpp"

#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace house_panel::panel_ui;

class FixtureMonotonicClock final : public MonotonicClock {
public:
	[[nodiscard]] std::chrono::steady_clock::time_point now() const noexcept override
	{
		return now_;
	}

	void advance(const std::chrono::milliseconds duration) { now_ += duration; }

private:
	std::chrono::steady_clock::time_point now_{std::chrono::steady_clock::time_point{}};
};

VoiceCatalogBinding fixture_voice_binding()
{
	return VoiceCatalogBinding{
		"catalog-fixture-1",
		"active-set-fixture-digest",
		"backend-fixture-digest",
		"recognizer-fixture-build-model",
		"policy-fixture-digest",
		{{"spoken-flip", "study.flip"},
		 {"spoken-next", "study.next"},
		 {"spoken-rate-good", "study.rate.good"}},
	};
}

std::vector<SiblingSurfaceContract> fixture_surfaces()
{
	return {
		{"study_surface",
		 {"study.flip", "study.next", "study.back", "study.rate.again",
		  "study.rate.hard", "study.rate.good", "study.rate.easy",
		  "study.rating.undo"}},
		{"agenda_surface",
		 {"agenda.start_wish_recording", "agenda.submit_wish"}},
	};
}

ShellControllerConfiguration fixture_configuration(const bool host_fixture_enabled = true)
{
	return ShellControllerConfiguration{
		std::chrono::milliseconds(100), "measured-conflict-window-fixture-1", 16,
		host_fixture_enabled};
}

PanelUiResult<std::unique_ptr<ShellController>>
create_fixture_controller(FixtureMonotonicClock &clock, const bool host_fixture_enabled = true)
{
	return ShellController::create(fixture_configuration(host_fixture_enabled),
				       fixture_voice_binding(), fixture_surfaces(), clock);
}

ShellStateSnapshot fixture_study_state()
{
	return ShellStateSnapshot{"context-fixture-1", "study_surface",
				  LifecycleMode::DayActive, ShellTransitionState::Stable, 7};
}

InteractionIntentEnvelope fixture_intent(const std::string &intent_id,
					 const IntentSourceKind source_kind,
					 const std::uint64_t source_sequence)
{
	InteractionIntentEnvelope intent{
		kInteractionIntentProtocolVersion,
		"context-fixture-1",
		std::string(to_string(source_kind)) + "-source-fixture",
		source_kind,
		source_sequence,
		1000 + source_sequence,
		7,
		"study_surface",
		intent_id,
		std::nullopt,
	};
	if (source_kind == IntentSourceKind::Voice) {
		intent.voice_evidence = VoiceIntentEvidence{
			"spoken-flip", "catalog-fixture-1", "active-set-fixture-digest",
			"backend-fixture-digest", "recognizer-fixture-build-model",
			"policy-fixture-digest"};
	}
	return intent;
}

bool is_rejection(const IntentProcessingOutcome &outcome, const PanelUiErrorCode error_code)
{
	return outcome.disposition != IntentDisposition::Accepted &&
	       outcome.visible_acknowledgement.kind == VisibleAcknowledgementKind::Rejected &&
	       outcome.rejection.has_value() && outcome.rejection->code() == error_code &&
	       !outcome.accepted_intent.has_value();
}

int test_touch_intent_is_accepted_with_visible_acknowledgement()
{
	FixtureMonotonicClock clock;
	auto controller = create_fixture_controller(clock);
	const auto outcome = controller.value()->process_intent(
		fixture_intent("study.flip", IntentSourceKind::Touch, 1), fixture_study_state());
	if (!(outcome.disposition == IntentDisposition::Accepted &&
		       outcome.visible_acknowledgement.kind == VisibleAcknowledgementKind::Accepted &&
		       outcome.visible_acknowledgement.indicator_id == "intent.accepted" &&
		       outcome.accepted_intent.has_value() && !outcome.rejection.has_value())) {
		return 1;
	}

	FixtureMonotonicClock fixture_clock;
	auto fixture_controller = create_fixture_controller(fixture_clock);
	const auto fixture_outcome = fixture_controller.value()->process_intent(
		fixture_intent("study.flip", IntentSourceKind::HostFixture, 1),
		fixture_study_state());
	return fixture_outcome.disposition == IntentDisposition::Accepted ? 0 : 1;
}

int test_voice_form_must_map_to_exact_intent_and_active_artifacts()
{
	FixtureMonotonicClock clock;
	auto controller = create_fixture_controller(clock);
	auto accepted = fixture_intent("study.flip", IntentSourceKind::Voice, 1);
	const auto accepted_outcome =
		controller.value()->process_intent(accepted, fixture_study_state());
	if (accepted_outcome.disposition != IntentDisposition::Accepted) {
		return 1;
	}

	FixtureMonotonicClock mismatch_clock;
	auto mismatch_controller = create_fixture_controller(mismatch_clock);
	auto mismatch = fixture_intent("study.next", IntentSourceKind::Voice, 1);
	const auto mismatch_outcome =
		mismatch_controller.value()->process_intent(mismatch, fixture_study_state());
	if (!is_rejection(mismatch_outcome,
			  PanelUiErrorCode::VoiceSpokenFormIntentMismatch)) {
		return 1;
	}

	FixtureMonotonicClock artifact_clock;
	auto artifact_controller = create_fixture_controller(artifact_clock);
	auto artifact_mismatch = fixture_intent("study.flip", IntentSourceKind::Voice, 1);
	artifact_mismatch.voice_evidence->active_command_set_digest = "different-fixture-digest";
	const auto artifact_outcome = artifact_controller.value()->process_intent(
		artifact_mismatch, fixture_study_state());
	if (!is_rejection(artifact_outcome,
			  PanelUiErrorCode::VoiceEvidenceBindingMismatch)) {
		return 1;
	}

	FixtureMonotonicClock unknown_form_clock;
	auto unknown_form_controller = create_fixture_controller(unknown_form_clock);
	auto unknown_form = fixture_intent("study.flip", IntentSourceKind::Voice, 1);
	unknown_form.voice_evidence->spoken_form_id = "spoken-form-not-in-active-set";
	const auto unknown_form_outcome = unknown_form_controller.value()->process_intent(
		unknown_form, fixture_study_state());
	return is_rejection(unknown_form_outcome, PanelUiErrorCode::VoiceSpokenFormUnknown) ? 0
												 : 1;
}

int test_invalid_intent_source_and_evidence_combinations_are_rejected()
{
	FixtureMonotonicClock unknown_clock;
	auto unknown_controller = create_fixture_controller(unknown_clock);
	const auto unknown = unknown_controller.value()->process_intent(
		fixture_intent("study.erase", IntentSourceKind::Touch, 1), fixture_study_state());
	if (!is_rejection(unknown, PanelUiErrorCode::IntentUnknown)) {
		return 1;
	}
	for (const auto &[key, value] : unknown.rejection->context()) {
		(void)key;
		if (value == "study.erase") {
			return 1;
		}
	}

	FixtureMonotonicClock source_clock;
	auto source_controller = create_fixture_controller(source_clock);
	auto forbidden_source =
		fixture_intent("study.rating.undo", IntentSourceKind::HostFixture, 1);
	const auto source_outcome = source_controller.value()->process_intent(
		forbidden_source, fixture_study_state());
	if (!is_rejection(source_outcome, PanelUiErrorCode::IntentSourceNotAllowed)) {
		return 1;
	}

	FixtureMonotonicClock evidence_clock;
	auto evidence_controller = create_fixture_controller(evidence_clock);
	auto unexpected_evidence = fixture_intent("study.flip", IntentSourceKind::Touch, 1);
	unexpected_evidence.voice_evidence = VoiceIntentEvidence{
		"spoken-flip", "catalog-fixture-1", "active-set-fixture-digest",
		"backend-fixture-digest", "recognizer-fixture-build-model",
		"policy-fixture-digest"};
	const auto evidence_outcome = evidence_controller.value()->process_intent(
		unexpected_evidence, fixture_study_state());
	return is_rejection(evidence_outcome, PanelUiErrorCode::VoiceEvidenceUnexpected) ? 0
												 : 1;
}

int test_nonmonotonic_source_sequence_is_rejected()
{
	FixtureMonotonicClock clock;
	auto controller = create_fixture_controller(clock);
	const auto first = controller.value()->process_intent(
		fixture_intent("study.flip", IntentSourceKind::Touch, 5), fixture_study_state());
	clock.advance(std::chrono::milliseconds(150));
	const auto replay = controller.value()->process_intent(
		fixture_intent("study.next", IntentSourceKind::Touch, 5), fixture_study_state());
	return first.disposition == IntentDisposition::Accepted &&
		       is_rejection(replay, PanelUiErrorCode::SourceSequenceNonmonotonic)
		       ? 0
		       : 1;
}

int test_stale_generation_context_and_surface_are_rejected()
{
	FixtureMonotonicClock generation_clock;
	auto generation_controller = create_fixture_controller(generation_clock);
	auto stale = fixture_intent("study.flip", IntentSourceKind::Touch, 1);
	stale.render_generation = 6;
	if (!is_rejection(generation_controller.value()->process_intent(
				  stale, fixture_study_state()),
			  PanelUiErrorCode::RenderGenerationMismatch)) {
		return 1;
	}

	FixtureMonotonicClock context_clock;
	auto context_controller = create_fixture_controller(context_clock);
	auto wrong_context = fixture_intent("study.flip", IntentSourceKind::Touch, 1);
	wrong_context.active_context_nonce = "stale-context-fixture";
	if (!is_rejection(context_controller.value()->process_intent(
				  wrong_context, fixture_study_state()),
			  PanelUiErrorCode::ActiveContextMismatch)) {
		return 1;
	}

	FixtureMonotonicClock surface_clock;
	auto surface_controller = create_fixture_controller(surface_clock);
	auto wrong_surface = fixture_intent("study.flip", IntentSourceKind::Touch, 1);
	wrong_surface.target_surface_id = "agenda_surface";
	return is_rejection(surface_controller.value()->process_intent(
				    wrong_surface, fixture_study_state()),
			    PanelUiErrorCode::SurfaceInactive)
		       ? 0
		       : 1;
}

int test_invalid_lifecycle_modes_reject_input()
{
	const LifecycleMode invalid_modes[]{LifecycleMode::NightRed, LifecycleMode::Suspended,
					    LifecycleMode::Diagnostic};
	for (const auto mode : invalid_modes) {
		FixtureMonotonicClock clock;
		auto controller = create_fixture_controller(clock);
		auto state = fixture_study_state();
		state.lifecycle_mode = mode;
		const auto outcome = controller.value()->process_intent(
			fixture_intent("study.flip", IntentSourceKind::Touch, 1), state);
		if (!is_rejection(outcome, PanelUiErrorCode::LifecycleInputDisabled)) {
			return 1;
		}
	}
	return 0;
}

int test_transition_input_is_rejected_and_never_queued()
{
	const ShellTransitionState transitions[]{
		ShellTransitionState::Loading, ShellTransitionState::Committing,
		ShellTransitionState::Suspending, ShellTransitionState::Resuming};
	for (const auto transition : transitions) {
		FixtureMonotonicClock clock;
		auto controller = create_fixture_controller(clock);
		auto transition_state = fixture_study_state();
		transition_state.transition_state = transition;
		const auto blocked = controller.value()->process_intent(
			fixture_intent("study.flip", IntentSourceKind::Touch, 1), transition_state);
		if (!is_rejection(blocked, PanelUiErrorCode::TransitionInputBlocked)) {
			return 1;
		}
		const auto accepted_after_transition = controller.value()->process_intent(
			fixture_intent("study.next", IntentSourceKind::Touch, 2), fixture_study_state());
		if (accepted_after_transition.disposition != IntentDisposition::Accepted ||
		    accepted_after_transition.accepted_intent->intent_id != "study.next") {
			return 1;
		}
	}
	return 0;
}

int test_first_wins_duplicate_and_conflict_ordering_is_deterministic()
{
	FixtureMonotonicClock clock;
	auto controller = create_fixture_controller(clock);
	const auto first = controller.value()->process_intent(
		fixture_intent("study.flip", IntentSourceKind::Touch, 1), fixture_study_state());
	clock.advance(std::chrono::milliseconds(50));
	const auto duplicate = controller.value()->process_intent(
		fixture_intent("study.flip", IntentSourceKind::Touch, 2), fixture_study_state());
	const auto conflict = controller.value()->process_intent(
		fixture_intent("study.next", IntentSourceKind::Touch, 3), fixture_study_state());
	clock.advance(std::chrono::milliseconds(51));
	const auto after_window = controller.value()->process_intent(
		fixture_intent("study.next", IntentSourceKind::Touch, 4), fixture_study_state());
	return first.disposition == IntentDisposition::Accepted &&
		       first.local_ingress_sequence == 1 &&
		       duplicate.disposition == IntentDisposition::Duplicate &&
		       duplicate.local_ingress_sequence == 2 &&
		       duplicate.rejection->code() == PanelUiErrorCode::IntentDuplicate &&
		       conflict.disposition == IntentDisposition::Conflict &&
		       conflict.local_ingress_sequence == 3 &&
		       conflict.rejection->code() == PanelUiErrorCode::IntentConflict &&
		       after_window.disposition == IntentDisposition::Accepted &&
		       after_window.local_ingress_sequence == 4
		       ? 0
		       : 1;
}

int test_reserved_agenda_effects_remain_touch_only_and_disabled()
{
	const std::string reserved_ids[]{"agenda.start_wish_recording", "agenda.submit_wish"};
	for (const auto &intent_id : reserved_ids) {
		FixtureMonotonicClock clock;
		auto controller = create_fixture_controller(clock);
		auto state = fixture_study_state();
		state.active_surface_id = "agenda_surface";
		auto touch_intent = fixture_intent(intent_id, IntentSourceKind::Touch, 1);
		touch_intent.target_surface_id = "agenda_surface";
		const auto touch_outcome = controller.value()->process_intent(touch_intent, state);
		if (!is_rejection(touch_outcome, PanelUiErrorCode::IntentDisabled)) {
			return 1;
		}

		FixtureMonotonicClock voice_clock;
		auto voice_controller = create_fixture_controller(voice_clock);
		auto voice_intent = fixture_intent(intent_id, IntentSourceKind::Voice, 1);
		voice_intent.target_surface_id = "agenda_surface";
		const auto voice_outcome = voice_controller.value()->process_intent(voice_intent, state);
		if (!is_rejection(voice_outcome, PanelUiErrorCode::IntentSourceNotAllowed)) {
			return 1;
		}
	}
	return 0;
}

int test_conflict_window_requires_explicit_evidence()
{
	FixtureMonotonicClock clock;
	auto configuration = fixture_configuration();
	configuration.conflict_window_evidence_id.clear();
	const auto controller = ShellController::create(
		configuration, fixture_voice_binding(), fixture_surfaces(), clock);
	return !controller && controller.error().code() ==
				      PanelUiErrorCode::ConfigurationConflictWindowEvidenceMissing
		       ? 0
		       : 1;
}

int test_host_fixture_source_requires_explicit_enablement()
{
	FixtureMonotonicClock clock;
	auto controller = create_fixture_controller(clock, false);
	const auto outcome = controller.value()->process_intent(
		fixture_intent("study.flip", IntentSourceKind::HostFixture, 1),
		fixture_study_state());
	return is_rejection(outcome, PanelUiErrorCode::HostFixtureSourceDisabled) ? 0 : 1;
}

} // namespace

int main()
{
	const struct {
		const char *name;
		int (*run)();
	} tests[] = {
		{"test_touch_intent_is_accepted_with_visible_acknowledgement",
		 test_touch_intent_is_accepted_with_visible_acknowledgement},
		{"test_voice_form_must_map_to_exact_intent_and_active_artifacts",
		 test_voice_form_must_map_to_exact_intent_and_active_artifacts},
		{"test_invalid_intent_source_and_evidence_combinations_are_rejected",
		 test_invalid_intent_source_and_evidence_combinations_are_rejected},
		{"test_nonmonotonic_source_sequence_is_rejected",
		 test_nonmonotonic_source_sequence_is_rejected},
		{"test_stale_generation_context_and_surface_are_rejected",
		 test_stale_generation_context_and_surface_are_rejected},
		{"test_invalid_lifecycle_modes_reject_input",
		 test_invalid_lifecycle_modes_reject_input},
		{"test_transition_input_is_rejected_and_never_queued",
		 test_transition_input_is_rejected_and_never_queued},
		{"test_first_wins_duplicate_and_conflict_ordering_is_deterministic",
		 test_first_wins_duplicate_and_conflict_ordering_is_deterministic},
		{"test_reserved_agenda_effects_remain_touch_only_and_disabled",
		 test_reserved_agenda_effects_remain_touch_only_and_disabled},
		{"test_conflict_window_requires_explicit_evidence",
		 test_conflict_window_requires_explicit_evidence},
		{"test_host_fixture_source_requires_explicit_enablement",
		 test_host_fixture_source_requires_explicit_enablement},
	};
	for (const auto &test : tests) {
		if (test.run() != 0) {
			std::cerr << test.name << " failed\n";
			return 1;
		}
	}
	return 0;
}
