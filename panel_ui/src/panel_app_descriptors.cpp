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
		 {},
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
		 {"shell.home"},
		 "study_state",
		 {"front", "back", "loading", "empty", "offline", "stale", "error", "queued", "saved", "rejected",
		  "provisional", "refill", "complete", "resume", "tracking_paused"}},
		{"agenda",
		 {
			 {"shell.open_agenda", IntentEffectClass::Navigation, true, true, false, true, "agenda"},
			 {"agenda.start_wish_recording", IntentEffectClass::CaptureStart, false, true, false, false},
			 {"agenda.submit_wish", IntentEffectClass::ExternalSubmission, false, true, false, false},
		 },
		 {"shell.home"},
		 "agenda_state",
		 {"today", "stale", "expired", "composer", "recording", "text_preview", "confirmation",
		  "delivery_unknown", "unavailable"}},
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
