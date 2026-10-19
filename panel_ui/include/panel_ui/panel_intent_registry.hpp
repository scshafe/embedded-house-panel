#pragma once

#include "panel_ui/interaction_intent.hpp"

#include <vector>
#include <string_view>

namespace house_panel::panel_ui {

// @canonical panel_intent_registry -- the complete closed version-1 semantic ID set,
// gathered from every app's compiled-in descriptor (panel_app_descriptor.hpp).
class PanelIntentRegistry final {
public:
	PanelIntentRegistry();

	[[nodiscard]] const IntentDefinition *find(std::string_view intent_id) const noexcept;
	[[nodiscard]] const std::vector<IntentDefinition> &definitions() const noexcept;

private:
	std::vector<IntentDefinition> definitions_;
};

} // namespace house_panel::panel_ui
