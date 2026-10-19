#include "panel_ui/panel_app_descriptor.hpp"
#include "panel_ui/panel_intent_registry.hpp"

#include <algorithm>
#include <iostream>
#include <set>
#include <string>

using namespace house_panel::panel_ui;

namespace {
int failures = 0;
void expect(bool value, const std::string &label)
{
	if (!value) {
		++failures;
		std::cerr << "FAIL: " << label << '\n';
	}
}
bool contains(const std::vector<std::string> &values, const std::string &value)
{
	return std::find(values.begin(), values.end(), value) != values.end();
}
} // namespace

int main()
{
	const auto &apps = panel_app_descriptors();
	expect(!apps.empty() && apps.front().surface_id == "home", "home is the first descriptor");
	std::set<std::string> surfaces;
	std::vector<std::string> concatenated;
	for (const auto &app : apps) {
		expect(surfaces.insert(app.surface_id).second, "surface IDs are unique: " + app.surface_id);
		std::size_t launches = 0;
		for (const auto &intent : app.intents) {
			concatenated.push_back(intent.intent_id);
			if (!intent.navigation_surface_id.empty()) {
				++launches;
				expect(intent.navigation_surface_id == app.surface_id,
				       intent.intent_id + " navigates to its own app's surface");
			}
		}
		expect(launches == 1, app.surface_id + " has exactly one navigation intent");
		expect(app.scenario_state_field.empty() == app.scenario_states.empty(),
		       app.surface_id + " names a scenario field exactly when it lists states");
	}
	const PanelIntentRegistry registry;
	std::vector<std::string> registered;
	for (const auto &definition : registry.definitions())
		registered.push_back(definition.intent_id);
	expect(registered == concatenated, "the registry is the descriptors' intents in order");
	expect(std::set<std::string>(registered.begin(), registered.end()).size() == registered.size(),
	       "intent IDs are unique");
	expect(launchable_surface_ids() == std::vector<std::string>{"study", "agenda"},
	       "the launchable surfaces are every app but home");
	const auto study = surface_routes(*find_app_descriptor("study"));
	expect(contains(study, "study.flip") && contains(study, "study.rating.undo") && contains(study, "shell.home") &&
		       !contains(study, "shell.open_study"),
	       "study routes its own actions and home, not its launcher");
	const auto agenda = surface_routes(*find_app_descriptor("agenda"));
	expect(agenda.size() == 3 && contains(agenda, "agenda.submit_wish") && contains(agenda, "shell.home"),
	       "agenda routes its two actions and home");
	expect(find_app_descriptor("weather") == nullptr, "an unknown surface has no descriptor");
	std::cout << "panel_app_descriptor: " << failures << " failures\n";
	return failures ? 1 : 0;
}
