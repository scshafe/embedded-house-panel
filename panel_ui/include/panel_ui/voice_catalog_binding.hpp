#pragma once

#include <string>
#include <vector>

namespace house_panel::panel_ui {

struct SpokenFormIntentBinding {
	std::string spoken_form_id;
	std::string intent_id;
};

/** Immutable identity and mapping of the one active spoken-command set. */
struct VoiceCatalogBinding {
	std::string catalog_revision;
	std::string active_command_set_digest;
	std::string backend_artifact_digest;
	std::string recognizer_build_model_identity;
	std::string recognition_policy_digest;
	std::vector<SpokenFormIntentBinding> spoken_form_bindings;
};

} // namespace house_panel::panel_ui
