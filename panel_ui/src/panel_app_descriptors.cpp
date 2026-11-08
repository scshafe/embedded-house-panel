#include "panel_ui/panel_app_descriptor.hpp"

namespace house_panel::panel_ui {

const std::vector<PanelAppDescriptor> &panel_app_descriptors()
{
	// shared/spoken_command_catalog/catalog.py reads the intent rows below as
	// text: keep each row's quoted ID and its effect class on one line, and
	// write no other quoted ID before an effect class in this file.
	static const std::vector<PanelAppDescriptor> descriptors{
		{"home",
		 {
			 {"shell.home", IntentEffectClass::Navigation, true, true, false, true, "home"},
			 {"shell.apps_previous", IntentEffectClass::Navigation, true, true, false, true},
			 {"shell.apps_next", IntentEffectClass::Navigation, true, true, false, true},
		 },
		 {"shell.open_report"},
		 {},
		 {}},
		{"study",
		 {
			 {"shell.open_study", IntentEffectClass::Navigation, true, true, false, true, "study"},
			 {"study.flip", IntentEffectClass::Navigation, true, true, true, true},
			 {"study.next", IntentEffectClass::Navigation, true, true, true, true},
			 {"study.back", IntentEffectClass::Navigation, true, true, true, true},
			 {"study.rate.again", IntentEffectClass::StudyRating, true, true, true, true},
			 {"study.rate.hard", IntentEffectClass::StudyRating, true, true, true, true},
			 {"study.rate.good", IntentEffectClass::StudyRating, true, true, true, true},
			 {"study.rate.easy", IntentEffectClass::StudyRating, true, true, true, true},
			 {"study.rating.undo", IntentEffectClass::RatingCorrection, true, true, false, false},
		 },
		 {"shell.home", "shell.open_report"},
		 "study_state",
		 {"front", "back", "loading", "empty", "offline", "stale", "error", "queued", "saved", "rejected",
		  "provisional", "refill", "complete", "resume", "tracking_paused"}},
		{"agenda",
		 {
			 {"shell.open_agenda", IntentEffectClass::Navigation, true, true, false, true, "agenda"},
			 {"agenda.start_wish_recording", IntentEffectClass::CaptureStart, false, true, false, false},
			 {"agenda.submit_wish", IntentEffectClass::ExternalSubmission, false, true, false, false},
		 },
		 {"shell.home", "shell.open_report"},
		 "agenda_state",
		 {"today", "stale", "expired", "composer", "recording", "text_preview", "confirmation",
		  "delivery_unknown", "unavailable"}},
		{"voice_lab",
		 {
			 {"shell.open_voice_lab", IntentEffectClass::Navigation, true, true, false, true, "voice_lab"},
			 {"voice_lab.record", IntentEffectClass::CaptureStart, true, true, false, true},
			 {"voice_lab.stop", IntentEffectClass::Navigation, true, true, false, true},
			 {"voice_lab.keep", IntentEffectClass::ExternalSubmission, true, true, false, true},
			 {"voice_lab.retake", IntentEffectClass::CaptureStart, true, true, false, true},
			 {"voice_lab.reject", IntentEffectClass::Navigation, true, true, false, true},
			 {"voice_lab.skip", IntentEffectClass::Navigation, true, true, false, true},
			 {"voice_lab.sign_in", IntentEffectClass::ExternalSubmission, true, true, false, true},
			 {"voice_lab.approve", IntentEffectClass::ExternalSubmission, true, true, false, true},
		 },
		 {"shell.home", "shell.open_report"},
		 "voice_lab_state",
		 {"signed_out", "signing_in", "approve", "ready", "recording", "review", "complete", "unavailable"}},
		// Spotify Connect on the owner's speaker (ADR 0013): touch only, the one configured speaker.
		{"music",
		 {
			 {"shell.open_music", IntentEffectClass::Navigation, true, true, false, true, "music"},
			 {"music.use_speaker", IntentEffectClass::ExternalSubmission, true, true, false, true},
			 {"music.play", IntentEffectClass::ExternalSubmission, true, true, false, true},
			 {"music.pause", IntentEffectClass::ExternalSubmission, true, true, false, true},
			 {"music.next", IntentEffectClass::ExternalSubmission, true, true, false, true},
			 {"music.volume_down", IntentEffectClass::ExternalSubmission, true, true, false, true},
			 {"music.volume_up", IntentEffectClass::ExternalSubmission, true, true, false, true},
		 },
		 {"shell.home", "shell.open_report"},
		 "music_state",
		 {"signed_out", "unavailable", "checking", "missing", "restricted", "ready", "playing", "paused"}},
		// The report screen: a typed description, then Send; the report goes only to the owner's PC.
		{"report",
		 {
			 // The day bar's Report button on every surface, and "Lexi report a bug" (bug-reports-and-self-repair.md):
			 // voice only opens the screen; recording a description needs a touch.
			 {"shell.open_report", IntentEffectClass::Navigation, true, true, true, true, "report"},
			 {"report.send", IntentEffectClass::ExternalSubmission, true, true, false, true},
			 // A spoken description: a touch starts it, inside the mic gate (step 3).
			 {"report.record", IntentEffectClass::CaptureStart, true, true, false, true},
			 {"report.stop", IntentEffectClass::Navigation, true, true, false, true},
			 {"report.cancel", IntentEffectClass::Navigation, true, true, false, true},
			 {"report.done", IntentEffectClass::Navigation, true, true, false, true},
			 {"report.show_list", IntentEffectClass::Navigation, true, true, false, true},
			 {"report.key.digit_1", IntentEffectClass::Navigation, true, true, false, true},
			 {"report.key.digit_2", IntentEffectClass::Navigation, true, true, false, true},
			 {"report.key.digit_3", IntentEffectClass::Navigation, true, true, false, true},
			 {"report.key.digit_4", IntentEffectClass::Navigation, true, true, false, true},
			 {"report.key.digit_5", IntentEffectClass::Navigation, true, true, false, true},
			 {"report.key.digit_6", IntentEffectClass::Navigation, true, true, false, true},
			 {"report.key.digit_7", IntentEffectClass::Navigation, true, true, false, true},
			 {"report.key.digit_8", IntentEffectClass::Navigation, true, true, false, true},
			 {"report.key.digit_9", IntentEffectClass::Navigation, true, true, false, true},
			 {"report.key.digit_0", IntentEffectClass::Navigation, true, true, false, true},
			 {"report.key.q", IntentEffectClass::Navigation, true, true, false, true},
			 {"report.key.w", IntentEffectClass::Navigation, true, true, false, true},
			 {"report.key.e", IntentEffectClass::Navigation, true, true, false, true},
			 {"report.key.r", IntentEffectClass::Navigation, true, true, false, true},
			 {"report.key.t", IntentEffectClass::Navigation, true, true, false, true},
			 {"report.key.y", IntentEffectClass::Navigation, true, true, false, true},
			 {"report.key.u", IntentEffectClass::Navigation, true, true, false, true},
			 {"report.key.i", IntentEffectClass::Navigation, true, true, false, true},
			 {"report.key.o", IntentEffectClass::Navigation, true, true, false, true},
			 {"report.key.p", IntentEffectClass::Navigation, true, true, false, true},
			 {"report.key.a", IntentEffectClass::Navigation, true, true, false, true},
			 {"report.key.s", IntentEffectClass::Navigation, true, true, false, true},
			 {"report.key.d", IntentEffectClass::Navigation, true, true, false, true},
			 {"report.key.f", IntentEffectClass::Navigation, true, true, false, true},
			 {"report.key.g", IntentEffectClass::Navigation, true, true, false, true},
			 {"report.key.h", IntentEffectClass::Navigation, true, true, false, true},
			 {"report.key.j", IntentEffectClass::Navigation, true, true, false, true},
			 {"report.key.k", IntentEffectClass::Navigation, true, true, false, true},
			 {"report.key.l", IntentEffectClass::Navigation, true, true, false, true},
			 {"report.key.z", IntentEffectClass::Navigation, true, true, false, true},
			 {"report.key.x", IntentEffectClass::Navigation, true, true, false, true},
			 {"report.key.c", IntentEffectClass::Navigation, true, true, false, true},
			 {"report.key.v", IntentEffectClass::Navigation, true, true, false, true},
			 {"report.key.b", IntentEffectClass::Navigation, true, true, false, true},
			 {"report.key.n", IntentEffectClass::Navigation, true, true, false, true},
			 {"report.key.m", IntentEffectClass::Navigation, true, true, false, true},
			 {"report.key.period", IntentEffectClass::Navigation, true, true, false, true},
			 {"report.key.comma", IntentEffectClass::Navigation, true, true, false, true},
			 {"report.key.question", IntentEffectClass::Navigation, true, true, false, true},
			 {"report.key.apostrophe", IntentEffectClass::Navigation, true, true, false, true},
			 {"report.key.space", IntentEffectClass::Navigation, true, true, false, true},
			 {"report.key.backspace", IntentEffectClass::Navigation, true, true, false, true},
		 },
		 {"shell.home"},
		 "report_state",
		 {"compose", "recording", "sending", "sent", "list", "unavailable"}},
	};
	return descriptors;
}

const PanelAppDescriptor *find_app_descriptor(const std::string_view surface_id) noexcept
{
	for (const auto &descriptor : panel_app_descriptors()) {
		if (descriptor.surface_id == surface_id) {
			return &descriptor;
		}
	}
	return nullptr;
}

std::vector<std::string> launchable_surface_ids()
{
	std::vector<std::string> surfaces;
	for (const auto &descriptor : panel_app_descriptors()) {
		if (descriptor.surface_id != "home") {
			surfaces.push_back(descriptor.surface_id);
		}
	}
	return surfaces;
}

std::vector<std::string> surface_routes(const PanelAppDescriptor &descriptor)
{
	std::vector<std::string> routes;
	for (const auto &intent : descriptor.intents) {
		if (intent.navigation_surface_id.empty()) {
			routes.push_back(intent.intent_id);
		}
	}
	routes.insert(routes.end(), descriptor.shared_routes.begin(), descriptor.shared_routes.end());
	return routes;
}

} // namespace house_panel::panel_ui
