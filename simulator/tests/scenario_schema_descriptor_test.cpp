// The checked-in scenario schema documents what read_scenario accepts; the app
// descriptors decide it. This keeps the two the same set for every surface field.
#include "panel_ui/panel_app_descriptor.hpp"
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <set>
#include <string>

int main()
{
	using house_panel::panel_ui::panel_app_descriptors;
	std::ifstream file(std::string(BHP_SIMULATOR_SOURCE_ROOT) + "/simulator/scenario.schema.json");
	const auto schema = nlohmann::json::parse(file);
	const auto &properties = schema.at("properties");
	int failures = 0;
	auto same = [&](const std::string &field, const std::set<std::string> &expected) {
		const auto values = properties.at(field).at("enum").get<std::set<std::string>>();
		if (values != expected) {
			++failures;
			std::cerr << "FAIL: scenario.schema.json " << field << " differs from the app descriptors\n";
		}
	};
	std::set<std::string> surfaces;
	for (const auto &app : panel_app_descriptors()) {
		surfaces.insert(app.surface_id);
		if (!app.scenario_state_field.empty())
			same(app.scenario_state_field, {app.scenario_states.begin(), app.scenario_states.end()});
	}
	same("surface", surfaces);
	std::cout << "scenario_schema_descriptor: " << failures << " failures\n";
	return failures ? 1 : 0;
}
