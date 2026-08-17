#include "panel_ui/interaction_intent.hpp"

namespace house_panel::panel_ui {

std::string_view to_string(const IntentSourceKind source_kind) noexcept
{
	switch (source_kind) {
	case IntentSourceKind::Touch:
		return "touch";
	case IntentSourceKind::Voice:
		return "voice";
	case IntentSourceKind::HostFixture:
		return "host_fixture";
	}
	return "unrecognized_source";
}

std::string_view to_string(const IntentEffectClass effect_class) noexcept
{
	switch (effect_class) {
	case IntentEffectClass::Navigation:
		return "navigation";
	case IntentEffectClass::StudyRating:
		return "study_rating";
	case IntentEffectClass::RatingCorrection:
		return "rating_correction";
	case IntentEffectClass::CaptureStart:
		return "capture_start";
	case IntentEffectClass::ExternalSubmission:
		return "external_submission";
	}
	return "unrecognized_effect_class";
}

} // namespace house_panel::panel_ui
