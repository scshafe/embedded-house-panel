#include "panel_ui/panel_intent_registry.hpp"

#include <iostream>
#include <map>
#include <string>

namespace {

using house_panel::panel_ui::IntentEffectClass;
using house_panel::panel_ui::PanelIntentRegistry;

struct ExpectedDefinition {
	IntentEffectClass effect_class;
	bool enabled;
	bool allow_touch;
	bool allow_voice;
	bool allow_host_fixture;
	std::string navigation_surface_id{};
};

int test_registry_contains_exact_version_one_union()
{
	const std::map<std::string, ExpectedDefinition> expected{
		{"shell.home", {IntentEffectClass::Navigation, true, true, false, true, "home"}},
		{"shell.open_study", {IntentEffectClass::Navigation, true, true, false, true, "study"}},
		{"shell.open_agenda", {IntentEffectClass::Navigation, true, true, false, true, "agenda"}},
		{"shell.apps_previous", {IntentEffectClass::Navigation, true, true, false, true}},
		{"shell.apps_next", {IntentEffectClass::Navigation, true, true, false, true}},
		{"study.flip", {IntentEffectClass::Navigation, true, true, true, true}},
		{"study.next", {IntentEffectClass::Navigation, true, true, true, true}},
		{"study.back", {IntentEffectClass::Navigation, true, true, true, true}},
		{"study.rate.again", {IntentEffectClass::StudyRating, true, true, true, true}},
		{"study.rate.hard", {IntentEffectClass::StudyRating, true, true, true, true}},
		{"study.rate.good", {IntentEffectClass::StudyRating, true, true, true, true}},
		{"study.rate.easy", {IntentEffectClass::StudyRating, true, true, true, true}},
		{"study.rating.undo",
		 {IntentEffectClass::RatingCorrection, true, true, false, false}},
		{"agenda.start_wish_recording",
		 {IntentEffectClass::CaptureStart, false, true, false, false}},
		{"agenda.submit_wish",
		 {IntentEffectClass::ExternalSubmission, false, true, false, false}},
		{"shell.open_voice_lab", {IntentEffectClass::Navigation, true, true, false, true, "voice_lab"}},
		{"voice_lab.record", {IntentEffectClass::CaptureStart, true, true, false, true}},
		{"voice_lab.stop", {IntentEffectClass::Navigation, true, true, false, true}},
		{"voice_lab.keep", {IntentEffectClass::ExternalSubmission, true, true, false, true}},
		{"voice_lab.retake", {IntentEffectClass::CaptureStart, true, true, false, true}},
		{"voice_lab.reject", {IntentEffectClass::Navigation, true, true, false, true}},
		{"voice_lab.skip", {IntentEffectClass::Navigation, true, true, false, true}},
		{"voice_lab.sign_in", {IntentEffectClass::ExternalSubmission, true, true, false, true}},
		{"voice_lab.approve", {IntentEffectClass::ExternalSubmission, true, true, false, true}},
	};
	const PanelIntentRegistry registry;
	if (registry.definitions().size() != expected.size()) {
		return 1;
	}
	for (const auto &[intent_id, expected_definition] : expected) {
		const auto *definition = registry.find(intent_id);
		if (definition == nullptr || definition->effect_class != expected_definition.effect_class ||
		    definition->enabled != expected_definition.enabled ||
		    definition->allow_touch != expected_definition.allow_touch ||
		    definition->allow_voice != expected_definition.allow_voice ||
		    definition->allow_host_fixture != expected_definition.allow_host_fixture ||
            definition->navigation_surface_id != expected_definition.navigation_surface_id) {
			return 1;
		}
	}
	return 0;
}

int test_registry_rejects_unknown_intent()
{
	const PanelIntentRegistry registry;
	return registry.find("study.delete_everything") == nullptr ? 0 : 1;
}

} // namespace

int main()
{
	if (test_registry_contains_exact_version_one_union() != 0) {
		std::cerr << "test_registry_contains_exact_version_one_union failed\n";
		return 1;
	}
	if (test_registry_rejects_unknown_intent() != 0) {
		std::cerr << "test_registry_rejects_unknown_intent failed\n";
		return 1;
	}
	return 0;
}
