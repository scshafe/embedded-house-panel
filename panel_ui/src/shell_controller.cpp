#include "panel_ui/shell_controller.hpp"

#include <algorithm>
#include <limits>
#include <set>
#include <utility>

namespace house_panel::panel_ui {
namespace {

PanelUiError configuration_error(const PanelUiErrorCode code, std::string message,
				 const std::string &field)
{
	return PanelUiError(code, std::move(message), {{"field", field}});
}

bool definition_allows_source(const IntentDefinition &definition,
			      const IntentSourceKind source_kind) noexcept
{
	switch (source_kind) {
	case IntentSourceKind::Touch:
		return definition.allow_touch;
	case IntentSourceKind::Voice:
		return definition.allow_voice;
	case IntentSourceKind::HostFixture:
		return definition.allow_host_fixture;
	}
	return false;
}

bool voice_binding_identity_is_complete(const VoiceCatalogBinding &binding) noexcept
{
	return !binding.catalog_revision.empty() && !binding.active_command_set_digest.empty() &&
	       !binding.backend_artifact_digest.empty() &&
	       !binding.recognizer_build_model_identity.empty() &&
	       !binding.recognition_policy_digest.empty();
}

} // namespace

PanelUiResult<std::unique_ptr<ShellController>> ShellController::create(
	ShellControllerConfiguration configuration,
	std::optional<VoiceCatalogBinding> voice_catalog_binding,
	std::vector<SiblingSurfaceContract> sibling_surfaces, MonotonicClock &monotonic_clock)
{
	if (configuration.measured_conflict_window.count() <= 0) {
		return PanelUiResult<std::unique_ptr<ShellController>>::failure(configuration_error(
			PanelUiErrorCode::ConfigurationConflictWindowInvalid,
			"The measured conflict window must be an explicit positive duration",
			"measured_conflict_window"));
	}
	if (configuration.conflict_window_evidence_id.empty()) {
		return PanelUiResult<std::unique_ptr<ShellController>>::failure(configuration_error(
			PanelUiErrorCode::ConfigurationConflictWindowEvidenceMissing,
			"The conflict window requires an evidence identifier",
			"conflict_window_evidence_id"));
	}
	if (configuration.maximum_source_instances == 0) {
		return PanelUiResult<std::unique_ptr<ShellController>>::failure(configuration_error(
			PanelUiErrorCode::ConfigurationSourceInstanceBoundInvalid,
			"The source-instance retention bound must be positive",
			"maximum_source_instances"));
	}
	if (sibling_surfaces.empty()) {
		return PanelUiResult<std::unique_ptr<ShellController>>::failure(configuration_error(
			PanelUiErrorCode::ConfigurationSurfaceInvalid,
			"At least one explicitly registered sibling surface is required",
			"sibling_surfaces"));
	}

	PanelIntentRegistry registry;
	std::set<std::string> surface_ids;
	for (const auto &surface : sibling_surfaces) {
		if (surface.surface_id.empty() || surface.routed_intent_ids.empty() ||
		    !surface_ids.insert(surface.surface_id).second) {
			return PanelUiResult<std::unique_ptr<ShellController>>::failure(
				configuration_error(PanelUiErrorCode::ConfigurationSurfaceInvalid,
						    "Every sibling surface requires a unique ID and at least one route",
						    "sibling_surfaces"));
		}
		std::set<std::string> routed_ids;
		for (const auto &intent_id : surface.routed_intent_ids) {
			if (registry.find(intent_id) == nullptr || !routed_ids.insert(intent_id).second) {
				return PanelUiResult<std::unique_ptr<ShellController>>::failure(
					configuration_error(PanelUiErrorCode::ConfigurationSurfaceInvalid,
							    "Surface routes must be unique canonical intent IDs",
							    "routed_intent_ids"));
			}
		}
	}

	if (voice_catalog_binding.has_value()) {
		const auto &binding = *voice_catalog_binding;
		if (!voice_binding_identity_is_complete(binding) ||
		    binding.spoken_form_bindings.empty()) {
			return PanelUiResult<std::unique_ptr<ShellController>>::failure(
				configuration_error(PanelUiErrorCode::ConfigurationVoiceBindingInvalid,
						    "Voice binding identity and mappings must be complete",
						    "voice_catalog_binding"));
		}
		std::set<std::string> spoken_form_ids;
		for (const auto &spoken_form_binding : binding.spoken_form_bindings) {
			const auto *definition = registry.find(spoken_form_binding.intent_id);
			if (spoken_form_binding.spoken_form_id.empty() || definition == nullptr ||
			    !definition->enabled || !definition->allow_voice ||
			    !spoken_form_ids.insert(spoken_form_binding.spoken_form_id).second) {
				return PanelUiResult<std::unique_ptr<ShellController>>::failure(
					configuration_error(PanelUiErrorCode::ConfigurationVoiceBindingInvalid,
							    "Voice mappings must be unique and voice-eligible",
							    "spoken_form_bindings"));
			}
		}
	}

	return PanelUiResult<std::unique_ptr<ShellController>>::success(
		std::unique_ptr<ShellController>(new ShellController(
			std::move(configuration), std::move(voice_catalog_binding),
			std::move(sibling_surfaces), monotonic_clock)));
}

ShellController::ShellController(ShellControllerConfiguration configuration,
				 std::optional<VoiceCatalogBinding> voice_catalog_binding,
				 std::vector<SiblingSurfaceContract> sibling_surfaces,
				 MonotonicClock &monotonic_clock)
	: configuration_(std::move(configuration)),
	  voice_catalog_binding_(std::move(voice_catalog_binding)),
	  sibling_surfaces_(std::move(sibling_surfaces)), monotonic_clock_(monotonic_clock)
{
}

std::optional<PanelUiError>
ShellController::validate_voice_evidence(const InteractionIntentEnvelope &intent) const
{
	if (intent.source_kind != IntentSourceKind::Voice) {
		if (intent.voice_evidence.has_value()) {
			return PanelUiError(PanelUiErrorCode::VoiceEvidenceUnexpected,
					    "Voice evidence is forbidden for this source kind",
					    {{"field", "voice_evidence"}});
		}
		return std::nullopt;
	}
	if (!intent.voice_evidence.has_value()) {
		return PanelUiError(PanelUiErrorCode::VoiceEvidenceMissing,
				    "Voice source requires complete catalog evidence",
				    {{"field", "voice_evidence"}});
	}
	if (!voice_catalog_binding_.has_value()) {
		return PanelUiError(PanelUiErrorCode::VoiceEvidenceBindingMismatch,
				    "No active voice catalog binding is configured",
				    {{"field", "voice_catalog_binding"}});
	}

	const auto &evidence = *intent.voice_evidence;
	const auto &binding = *voice_catalog_binding_;
	if (evidence.catalog_revision != binding.catalog_revision ||
	    evidence.active_command_set_digest != binding.active_command_set_digest ||
	    evidence.backend_artifact_digest != binding.backend_artifact_digest ||
	    evidence.recognizer_build_model_identity != binding.recognizer_build_model_identity ||
	    evidence.recognition_policy_digest != binding.recognition_policy_digest) {
		return PanelUiError(PanelUiErrorCode::VoiceEvidenceBindingMismatch,
				    "Voice evidence does not match the configured active artifact chain",
				    {{"field", "voice_evidence_identity"}});
	}

	const auto mapping = std::find_if(
		binding.spoken_form_bindings.begin(), binding.spoken_form_bindings.end(),
		[&evidence](const SpokenFormIntentBinding &candidate) {
			return candidate.spoken_form_id == evidence.spoken_form_id;
		});
	if (mapping == binding.spoken_form_bindings.end()) {
		return PanelUiError(PanelUiErrorCode::VoiceSpokenFormUnknown,
				    "The spoken form is not in the bound active command set",
				    {{"field", "spoken_form_id"}});
	}
	if (mapping->intent_id != intent.intent_id) {
		return PanelUiError(PanelUiErrorCode::VoiceSpokenFormIntentMismatch,
				    "The spoken form does not map to the envelope intent",
				    {{"field", "intent_id"}});
	}
	return std::nullopt;
}

const SiblingSurfaceContract *
ShellController::find_surface(const std::string_view surface_id) const noexcept
{
	const auto surface = std::find_if(
		sibling_surfaces_.begin(), sibling_surfaces_.end(),
		[surface_id](const SiblingSurfaceContract &candidate) {
			return candidate.surface_id == surface_id;
		});
	return surface == sibling_surfaces_.end() ? nullptr : &*surface;
}

bool ShellController::surface_routes_intent(const SiblingSurfaceContract &surface,
					    const std::string_view intent_id) const noexcept
{
	return std::find(surface.routed_intent_ids.begin(), surface.routed_intent_ids.end(),
			 intent_id) != surface.routed_intent_ids.end();
}

IntentProcessingOutcome ShellController::reject(const IntentDisposition disposition,
						 PanelUiError error) const
{
	const std::string indicator_id =
		disposition == IntentDisposition::Duplicate
			? "intent.duplicate"
			: disposition == IntentDisposition::Conflict ? "intent.conflict"
								  : "intent.rejected";
	return IntentProcessingOutcome{latest_local_ingress_sequence_, disposition,
				       {VisibleAcknowledgementKind::Rejected, indicator_id},
				       std::nullopt, std::move(error)};
}

IntentProcessingOutcome
ShellController::process_intent(const InteractionIntentEnvelope &intent,
				const ShellStateSnapshot &shell_state)
{
	if (latest_local_ingress_sequence_ == std::numeric_limits<std::uint64_t>::max()) {
		return reject(IntentDisposition::Rejected,
			      PanelUiError(PanelUiErrorCode::IngressSequenceExhausted,
					   "Local ingress sequence is exhausted",
					   {{"field", "local_ingress_sequence"}}));
	}
	++latest_local_ingress_sequence_;
	if (intent.protocol_version != kInteractionIntentProtocolVersion) {
		return reject(IntentDisposition::Rejected,
			      PanelUiError(PanelUiErrorCode::ProtocolVersionUnsupported,
					   "Interaction intent protocol version is unsupported",
					   {{"field", "protocol_version"}}));
	}
	if (intent.source_instance_id.empty() || intent.source_sequence == 0 ||
	    intent.observed_monotonic_time_ns == 0) {
		return reject(IntentDisposition::Rejected,
			      PanelUiError(PanelUiErrorCode::SourceInstanceInvalid,
					   "Source identity, sequence, and observed time must be explicit",
					   {{"field", "source_identity"}}));
	}

	const auto *definition = intent_registry_.find(intent.intent_id);
	if (definition == nullptr) {
		return reject(IntentDisposition::Rejected,
			      PanelUiError(PanelUiErrorCode::IntentUnknown,
					   "Intent ID is not in the closed protocol registry",
					   {{"field", "intent_id"}}));
	}
	if (intent.source_kind == IntentSourceKind::HostFixture &&
	    !configuration_.host_fixture_source_enabled) {
		return reject(IntentDisposition::Rejected,
			      PanelUiError(PanelUiErrorCode::HostFixtureSourceDisabled,
					   "Host-fixture ingress is disabled",
					   {{"source_kind", "host_fixture"}}));
	}
	if (!definition_allows_source(*definition, intent.source_kind)) {
		return reject(IntentDisposition::Rejected,
			      PanelUiError(PanelUiErrorCode::IntentSourceNotAllowed,
					   "Intent is not permitted for this source kind",
					   {{"source_kind", std::string(to_string(intent.source_kind))}}));
	}
	if (const auto voice_error = validate_voice_evidence(intent); voice_error.has_value()) {
		return reject(IntentDisposition::Rejected, *voice_error);
	}
	if (!definition->enabled) {
		return reject(IntentDisposition::Rejected,
			      PanelUiError(PanelUiErrorCode::IntentDisabled,
					   "The registered intent is not enabled",
					   {{"field", "intent_id"}}));
	}
	if (intent.active_context_nonce.empty() ||
	    intent.active_context_nonce != shell_state.active_context_nonce) {
		return reject(IntentDisposition::Rejected,
			      PanelUiError(PanelUiErrorCode::ActiveContextMismatch,
					   "Intent does not bind the active shell context",
					   {{"field", "active_context_nonce"}}));
	}
	if (intent.render_generation == 0 ||
	    intent.render_generation != shell_state.render_generation) {
		return reject(IntentDisposition::Rejected,
			      PanelUiError(PanelUiErrorCode::RenderGenerationMismatch,
					   "Intent does not bind the active render generation",
					   {{"field", "render_generation"}}));
	}
	if (intent.target_surface_id.empty() ||
	    intent.target_surface_id != shell_state.active_surface_id) {
		return reject(IntentDisposition::Rejected,
			      PanelUiError(PanelUiErrorCode::SurfaceInactive,
					   "Intent target is not the active surface",
					   {{"field", "target_surface_id"}}));
	}
	const auto *surface = find_surface(shell_state.active_surface_id);
	if (surface == nullptr) {
		return reject(IntentDisposition::Rejected,
			      PanelUiError(PanelUiErrorCode::SurfaceInactive,
					   "Active surface is not registered",
					   {{"field", "active_surface_id"}}));
	}
	if (!surface_routes_intent(*surface, intent.intent_id)) {
		return reject(IntentDisposition::Rejected,
			      PanelUiError(PanelUiErrorCode::SurfaceIntentUnsupported,
					   "Active surface does not route this intent",
					   {{"field", "intent_id"}}));
	}

	const auto source_sequence = latest_source_sequences_.find(intent.source_instance_id);
	if (source_sequence != latest_source_sequences_.end() &&
	    intent.source_sequence <= source_sequence->second) {
		return reject(IntentDisposition::Rejected,
			      PanelUiError(PanelUiErrorCode::SourceSequenceNonmonotonic,
					   "Source sequence must increase monotonically",
					   {{"field", "source_sequence"}}));
	}
	if (source_sequence == latest_source_sequences_.end() &&
	    latest_source_sequences_.size() >= configuration_.maximum_source_instances) {
		return reject(IntentDisposition::Rejected,
			      PanelUiError(PanelUiErrorCode::SourceInstanceCapacityExceeded,
					   "Source-instance retention capacity is exhausted",
					   {{"field", "source_instance_id"}}));
	}
	latest_source_sequences_[intent.source_instance_id] = intent.source_sequence;

	// Record ingress before lifecycle rejection so a transition-time command cannot
	// be replayed after the transition and act as an implicit queue.
	if (shell_state.lifecycle_mode != LifecycleMode::DayActive) {
		return reject(IntentDisposition::Rejected,
			      PanelUiError(PanelUiErrorCode::LifecycleInputDisabled,
					   "Application input is disabled for this lifecycle mode",
					   {{"lifecycle_mode",
					     std::string(bhp::panel_mode_protocol::to_string(
						     shell_state.lifecycle_mode))}}));
	}
	if (shell_state.transition_state != ShellTransitionState::Stable) {
		return reject(IntentDisposition::Rejected,
			      PanelUiError(PanelUiErrorCode::TransitionInputBlocked,
					   "Application input is rejected during shell transition",
					   {{"transition_state",
					     std::string(to_string(shell_state.transition_state))}}));
	}

	const auto ingress_time = monotonic_clock_.now();
	if (last_accepted_intent_.has_value() &&
	    ingress_time < last_accepted_intent_->accepted_at) {
		return reject(IntentDisposition::Rejected,
			      PanelUiError(PanelUiErrorCode::IngressClockRegressed,
					   "Injected monotonic clock regressed",
					   {{"field", "monotonic_clock"}}));
	}
	if (last_accepted_intent_.has_value() &&
	    last_accepted_intent_->active_context_nonce == shell_state.active_context_nonce &&
	    last_accepted_intent_->active_surface_id == shell_state.active_surface_id &&
	    last_accepted_intent_->render_generation == shell_state.render_generation &&
	    ingress_time - last_accepted_intent_->accepted_at <=
		configuration_.measured_conflict_window) {
		if (last_accepted_intent_->intent_id == intent.intent_id) {
			return reject(IntentDisposition::Duplicate,
				      PanelUiError(PanelUiErrorCode::IntentDuplicate,
						   "Matching intent arrived inside the conflict window",
						   {{"field", "intent_id"}}));
		}
		return reject(IntentDisposition::Conflict,
			      PanelUiError(PanelUiErrorCode::IntentConflict,
					   "Different intent arrived inside the conflict window",
					   {{"field", "intent_id"}}));
	}

	last_accepted_intent_ = LastAcceptedIntent{
		intent.intent_id, shell_state.active_context_nonce, shell_state.active_surface_id,
		shell_state.render_generation, ingress_time};
	return IntentProcessingOutcome{latest_local_ingress_sequence_, IntentDisposition::Accepted,
				       {VisibleAcknowledgementKind::Accepted, "intent.accepted"},
				       intent, std::nullopt};
}

} // namespace house_panel::panel_ui
