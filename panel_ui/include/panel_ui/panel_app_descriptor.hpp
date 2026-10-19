#pragma once

#include "panel_ui/interaction_intent.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace house_panel::panel_ui {

/**
 * @canonical panel_app_descriptor -- one compiled-in record per panel app.
 *
 * Adding an app: append its descriptor in `panel_app_descriptors.cpp`, give its
 * surface a renderer in the LVGL surface table, and add a catalog entry in
 * `panel_ui/config/applications.json`. The intent registry, the implemented
 * surface list, the shell's surface routes and the scenario fields all read
 * these records. There is still no plugin loader.
 */
struct PanelAppDescriptor {
	std::string surface_id;
	/** This app's intents. A launch intent navigates to `surface_id`. */
	std::vector<IntentDefinition> intents;
	/** Routes this surface accepts besides its own non-launch intents. */
	std::vector<std::string> shared_routes;
	/** The scenario field holding this surface's fixture state, and its values; empty when none. */
	std::string scenario_state_field;
	std::vector<std::string> scenario_states;
};

/** Every app in registry order: home first, then each launchable app. */
[[nodiscard]] const std::vector<PanelAppDescriptor> &panel_app_descriptors();
[[nodiscard]] const PanelAppDescriptor *find_app_descriptor(std::string_view surface_id) noexcept;
/** Surfaces a catalog entry may launch: every descriptor except home. */
[[nodiscard]] std::vector<std::string> launchable_surface_ids();
/** The ShellController routes for an app surface: its own non-launch intents, then its shared routes. */
[[nodiscard]] std::vector<std::string> surface_routes(const PanelAppDescriptor &descriptor);

} // namespace house_panel::panel_ui
