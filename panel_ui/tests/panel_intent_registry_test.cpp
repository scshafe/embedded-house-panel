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
		{"shell.open_music", {IntentEffectClass::Navigation, true, true, false, true, "music"}},
		{"music.use_speaker", {IntentEffectClass::ExternalSubmission, true, true, false, true}},
		{"music.play", {IntentEffectClass::ExternalSubmission, true, true, false, true}},
		{"music.pause", {IntentEffectClass::ExternalSubmission, true, true, false, true}},
		{"music.next", {IntentEffectClass::ExternalSubmission, true, true, false, true}},
		{"music.volume_down", {IntentEffectClass::ExternalSubmission, true, true, false, true}},
		{"music.volume_up", {IntentEffectClass::ExternalSubmission, true, true, false, true}},
		{"shell.open_report", {IntentEffectClass::Navigation, true, true, true, true, "report"}},
		{"report.send", {IntentEffectClass::ExternalSubmission, true, true, false, true}},
		{"report.record", {IntentEffectClass::CaptureStart, true, true, false, true}},
		{"report.stop", {IntentEffectClass::Navigation, true, true, false, true}},
		{"report.cancel", {IntentEffectClass::Navigation, true, true, false, true}},
		{"report.done", {IntentEffectClass::Navigation, true, true, false, true}},
		{"report.show_list", {IntentEffectClass::Navigation, true, true, false, true}},
		{"report.key.digit_1", {IntentEffectClass::Navigation, true, true, false, true}},
		{"report.key.digit_2", {IntentEffectClass::Navigation, true, true, false, true}},
		{"report.key.digit_3", {IntentEffectClass::Navigation, true, true, false, true}},
		{"report.key.digit_4", {IntentEffectClass::Navigation, true, true, false, true}},
		{"report.key.digit_5", {IntentEffectClass::Navigation, true, true, false, true}},
		{"report.key.digit_6", {IntentEffectClass::Navigation, true, true, false, true}},
		{"report.key.digit_7", {IntentEffectClass::Navigation, true, true, false, true}},
		{"report.key.digit_8", {IntentEffectClass::Navigation, true, true, false, true}},
		{"report.key.digit_9", {IntentEffectClass::Navigation, true, true, false, true}},
		{"report.key.digit_0", {IntentEffectClass::Navigation, true, true, false, true}},
		{"report.key.q", {IntentEffectClass::Navigation, true, true, false, true}},
		{"report.key.w", {IntentEffectClass::Navigation, true, true, false, true}},
		{"report.key.e", {IntentEffectClass::Navigation, true, true, false, true}},
		{"report.key.r", {IntentEffectClass::Navigation, true, true, false, true}},
		{"report.key.t", {IntentEffectClass::Navigation, true, true, false, true}},
		{"report.key.y", {IntentEffectClass::Navigation, true, true, false, true}},
		{"report.key.u", {IntentEffectClass::Navigation, true, true, false, true}},
		{"report.key.i", {IntentEffectClass::Navigation, true, true, false, true}},
		{"report.key.o", {IntentEffectClass::Navigation, true, true, false, true}},
		{"report.key.p", {IntentEffectClass::Navigation, true, true, false, true}},
		{"report.key.a", {IntentEffectClass::Navigation, true, true, false, true}},
		{"report.key.s", {IntentEffectClass::Navigation, true, true, false, true}},
		{"report.key.d", {IntentEffectClass::Navigation, true, true, false, true}},
		{"report.key.f", {IntentEffectClass::Navigation, true, true, false, true}},
		{"report.key.g", {IntentEffectClass::Navigation, true, true, false, true}},
		{"report.key.h", {IntentEffectClass::Navigation, true, true, false, true}},
		{"report.key.j", {IntentEffectClass::Navigation, true, true, false, true}},
		{"report.key.k", {IntentEffectClass::Navigation, true, true, false, true}},
		{"report.key.l", {IntentEffectClass::Navigation, true, true, false, true}},
		{"report.key.z", {IntentEffectClass::Navigation, true, true, false, true}},
		{"report.key.x", {IntentEffectClass::Navigation, true, true, false, true}},
		{"report.key.c", {IntentEffectClass::Navigation, true, true, false, true}},
		{"report.key.v", {IntentEffectClass::Navigation, true, true, false, true}},
		{"report.key.b", {IntentEffectClass::Navigation, true, true, false, true}},
		{"report.key.n", {IntentEffectClass::Navigation, true, true, false, true}},
		{"report.key.m", {IntentEffectClass::Navigation, true, true, false, true}},
		{"report.key.period", {IntentEffectClass::Navigation, true, true, false, true}},
		{"report.key.comma", {IntentEffectClass::Navigation, true, true, false, true}},
		{"report.key.question", {IntentEffectClass::Navigation, true, true, false, true}},
		{"report.key.apostrophe", {IntentEffectClass::Navigation, true, true, false, true}},
		{"report.key.space", {IntentEffectClass::Navigation, true, true, false, true}},
		{"report.key.backspace", {IntentEffectClass::Navigation, true, true, false, true}},
		{"shell.open_weather", {IntentEffectClass::Navigation, true, true, false, true, "weather"}},
		{"weather.unit.fahrenheit", {IntentEffectClass::Navigation, true, true, false, true}},
		{"weather.unit.celsius", {IntentEffectClass::Navigation, true, true, false, true}},
		{"weather.unit.kelvin", {IntentEffectClass::Navigation, true, true, false, true}},
		{"weather.bar.temperatures", {IntentEffectClass::Navigation, true, true, false, true}},
		{"weather.bar.sun_times", {IntentEffectClass::Navigation, true, true, false, true}},
		{"weather.location.automatic", {IntentEffectClass::ExternalSubmission, true, true, false, true}},
		{"weather.location.choose", {IntentEffectClass::Navigation, true, true, false, true}},
		{"weather.search.submit", {IntentEffectClass::ExternalSubmission, true, true, false, true}},
		{"weather.search.back", {IntentEffectClass::Navigation, true, true, false, true}},
		{"weather.result.1", {IntentEffectClass::Navigation, true, true, false, true}},
		{"weather.result.2", {IntentEffectClass::Navigation, true, true, false, true}},
		{"weather.result.3", {IntentEffectClass::Navigation, true, true, false, true}},
		{"weather.result.4", {IntentEffectClass::Navigation, true, true, false, true}},
		{"weather.result.5", {IntentEffectClass::Navigation, true, true, false, true}},
		{"weather.done", {IntentEffectClass::Navigation, true, true, false, true}},
		{"weather.key.a", {IntentEffectClass::Navigation, true, true, false, true}},
		{"weather.key.b", {IntentEffectClass::Navigation, true, true, false, true}},
		{"weather.key.c", {IntentEffectClass::Navigation, true, true, false, true}},
		{"weather.key.d", {IntentEffectClass::Navigation, true, true, false, true}},
		{"weather.key.e", {IntentEffectClass::Navigation, true, true, false, true}},
		{"weather.key.f", {IntentEffectClass::Navigation, true, true, false, true}},
		{"weather.key.g", {IntentEffectClass::Navigation, true, true, false, true}},
		{"weather.key.h", {IntentEffectClass::Navigation, true, true, false, true}},
		{"weather.key.i", {IntentEffectClass::Navigation, true, true, false, true}},
		{"weather.key.j", {IntentEffectClass::Navigation, true, true, false, true}},
		{"weather.key.k", {IntentEffectClass::Navigation, true, true, false, true}},
		{"weather.key.l", {IntentEffectClass::Navigation, true, true, false, true}},
		{"weather.key.m", {IntentEffectClass::Navigation, true, true, false, true}},
		{"weather.key.n", {IntentEffectClass::Navigation, true, true, false, true}},
		{"weather.key.o", {IntentEffectClass::Navigation, true, true, false, true}},
		{"weather.key.p", {IntentEffectClass::Navigation, true, true, false, true}},
		{"weather.key.q", {IntentEffectClass::Navigation, true, true, false, true}},
		{"weather.key.r", {IntentEffectClass::Navigation, true, true, false, true}},
		{"weather.key.s", {IntentEffectClass::Navigation, true, true, false, true}},
		{"weather.key.t", {IntentEffectClass::Navigation, true, true, false, true}},
		{"weather.key.u", {IntentEffectClass::Navigation, true, true, false, true}},
		{"weather.key.v", {IntentEffectClass::Navigation, true, true, false, true}},
		{"weather.key.w", {IntentEffectClass::Navigation, true, true, false, true}},
		{"weather.key.x", {IntentEffectClass::Navigation, true, true, false, true}},
		{"weather.key.y", {IntentEffectClass::Navigation, true, true, false, true}},
		{"weather.key.z", {IntentEffectClass::Navigation, true, true, false, true}},
		{"weather.key.hyphen", {IntentEffectClass::Navigation, true, true, false, true}},
		{"weather.key.apostrophe", {IntentEffectClass::Navigation, true, true, false, true}},
		{"weather.key.space", {IntentEffectClass::Navigation, true, true, false, true}},
		{"weather.key.backspace", {IntentEffectClass::Navigation, true, true, false, true}},
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
