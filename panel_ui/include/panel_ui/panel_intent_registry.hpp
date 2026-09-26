#pragma once

#include "panel_ui/interaction_intent.hpp"

#include <array>
#include <string_view>

namespace house_panel::panel_ui {

// @canonical panel_intent_registry -- the complete closed version-1 semantic ID set.
class PanelIntentRegistry final {
public:
	static constexpr std::size_t kDefinitionCount = 15;

	PanelIntentRegistry();

	[[nodiscard]] const IntentDefinition *find(std::string_view intent_id) const noexcept;
	[[nodiscard]] const std::array<IntentDefinition, kDefinitionCount> &definitions() const noexcept;

private:
	std::array<IntentDefinition, kDefinitionCount> definitions_;
};

} // namespace house_panel::panel_ui
