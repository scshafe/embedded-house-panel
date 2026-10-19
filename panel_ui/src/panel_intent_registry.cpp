#include "panel_ui/panel_intent_registry.hpp"

#include "panel_ui/panel_app_descriptor.hpp"

namespace house_panel::panel_ui {

PanelIntentRegistry::PanelIntentRegistry()
{
	for (const auto &descriptor : panel_app_descriptors()) {
		definitions_.insert(definitions_.end(), descriptor.intents.begin(), descriptor.intents.end());
	}
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

const std::vector<IntentDefinition> &PanelIntentRegistry::definitions() const noexcept
{
	return definitions_;
}

} // namespace house_panel::panel_ui
