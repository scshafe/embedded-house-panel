#pragma once

#include "panel_ui/interaction_intent.hpp"
#include "panel_ui/monotonic_clock.hpp"
#include "panel_ui/panel_intent_registry.hpp"
#include "panel_ui/result.hpp"
#include "panel_ui/shell_state.hpp"
#include "panel_ui/sibling_surface_contract.hpp"
#include "panel_ui/voice_catalog_binding.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace house_panel::panel_ui {

struct ShellControllerConfiguration {
	std::chrono::milliseconds measured_conflict_window;
	std::string conflict_window_evidence_id;
	std::size_t maximum_source_instances;
	bool host_fixture_source_enabled;
};

enum class IntentDisposition { Accepted, Duplicate, Conflict, Rejected };

enum class VisibleAcknowledgementKind { Accepted, Rejected };

struct VisibleIntentAcknowledgement {
	VisibleAcknowledgementKind kind;
	std::string indicator_id;
};

struct IntentProcessingOutcome {
	std::uint64_t local_ingress_sequence;
	IntentDisposition disposition;
	VisibleIntentAcknowledgement visible_acknowledgement;
	std::optional<InteractionIntentEnvelope> accepted_intent;
	std::optional<PanelUiError> rejection;
};

// @canonical panel_shell_controller -- validates and orders every local semantic intent.
class ShellController final {
public:
	[[nodiscard]] static PanelUiResult<std::unique_ptr<ShellController>> create(
		ShellControllerConfiguration configuration,
		std::optional<VoiceCatalogBinding> voice_catalog_binding,
		std::vector<SiblingSurfaceContract> sibling_surfaces,
		MonotonicClock &monotonic_clock);

	/**
	 * Validates and orders one intent against an immutable shell-state snapshot.
	 * The method never queues work. Expected rejection is returned as a visible,
	 * typed outcome rather than an exception.
	 */
	[[nodiscard]] IntentProcessingOutcome
	process_intent(const InteractionIntentEnvelope &intent,
		       const ShellStateSnapshot &shell_state);

	/** Rebinds a validated active voice artifact without resetting replay or intent order. */
	[[nodiscard]] PanelUiResult<bool> update_voice_binding(std::optional<VoiceCatalogBinding> binding);

private:
	struct LastAcceptedIntent {
		std::string intent_id;
		std::string active_context_nonce;
		std::string active_surface_id;
		std::uint64_t render_generation;
		std::chrono::steady_clock::time_point accepted_at;
	};

	ShellController(ShellControllerConfiguration configuration,
			std::optional<VoiceCatalogBinding> voice_catalog_binding,
			std::vector<SiblingSurfaceContract> sibling_surfaces,
			MonotonicClock &monotonic_clock);

	[[nodiscard]] std::optional<PanelUiError>
	validate_voice_evidence(const InteractionIntentEnvelope &intent) const;
	[[nodiscard]] const SiblingSurfaceContract *
	find_surface(std::string_view surface_id) const noexcept;
	[[nodiscard]] bool surface_routes_intent(const SiblingSurfaceContract &surface,
					 std::string_view intent_id) const noexcept;
	[[nodiscard]] IntentProcessingOutcome reject(IntentDisposition disposition,
						    PanelUiError error) const;

	ShellControllerConfiguration configuration_;
	std::optional<VoiceCatalogBinding> voice_catalog_binding_;
	std::vector<SiblingSurfaceContract> sibling_surfaces_;
	MonotonicClock &monotonic_clock_;
	PanelIntentRegistry intent_registry_;
	std::map<std::string, std::uint64_t> latest_source_sequences_;
	std::uint64_t latest_local_ingress_sequence_{0};
	std::optional<LastAcceptedIntent> last_accepted_intent_;
};

} // namespace house_panel::panel_ui
