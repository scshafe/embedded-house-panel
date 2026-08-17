#include "panel_ui/panel_intent_registry.hpp"

namespace house_panel::panel_ui {

PanelIntentRegistry::PanelIntentRegistry()
	: definitions_{{
		{"study.flip", IntentEffectClass::Navigation, true, true, true, true},
		{"study.next", IntentEffectClass::Navigation, true, true, true, true},
		{"study.back", IntentEffectClass::Navigation, true, true, true, true},
		{"study.rate.again", IntentEffectClass::StudyRating, true, true, true, true},
		{"study.rate.hard", IntentEffectClass::StudyRating, true, true, true, true},
		{"study.rate.good", IntentEffectClass::StudyRating, true, true, true, true},
		{"study.rate.easy", IntentEffectClass::StudyRating, true, true, true, true},
		{"study.rating.undo", IntentEffectClass::RatingCorrection, true, true, false,
		 false},
		{"agenda.start_wish_recording", IntentEffectClass::CaptureStart, false, true,
		 false, false},
		{"agenda.submit_wish", IntentEffectClass::ExternalSubmission, false, true,
		 false, false},
	}}
{
}

const IntentDefinition *PanelIntentRegistry::find(const std::string_view intent_id) const noexcept
{
	for (const auto &definition : definitions_) {
		if (definition.intent_id == intent_id) {
			return &definition;
		}
	}
	return nullptr;
}

const std::array<IntentDefinition, PanelIntentRegistry::kDefinitionCount> &
PanelIntentRegistry::definitions() const noexcept
{
	return definitions_;
}

} // namespace house_panel::panel_ui
