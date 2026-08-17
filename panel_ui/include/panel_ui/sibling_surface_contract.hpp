#pragma once

#include <string>
#include <vector>

namespace house_panel::panel_ui {

// @extension-point panel_surface_contract -- declares one sibling surface's routable intents.
struct SiblingSurfaceContract {
	std::string surface_id;
	std::vector<std::string> routed_intent_ids;
};

} // namespace house_panel::panel_ui
