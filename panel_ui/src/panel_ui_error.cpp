#include "panel_ui/panel_ui_error.hpp"

#include <utility>

namespace house_panel::panel_ui {

std::string_view to_string(const PanelUiErrorCode code) noexcept
{
	switch (code) {
	case PanelUiErrorCode::ConfigurationConflictWindowInvalid:
		return "PANEL_UI.CONFIGURATION_CONFLICT_WINDOW_INVALID";
	case PanelUiErrorCode::ConfigurationConflictWindowEvidenceMissing:
		return "PANEL_UI.CONFIGURATION_CONFLICT_WINDOW_EVIDENCE_MISSING";
	case PanelUiErrorCode::ConfigurationSourceInstanceBoundInvalid:
		return "PANEL_UI.CONFIGURATION_SOURCE_INSTANCE_BOUND_INVALID";
	case PanelUiErrorCode::ConfigurationSurfaceInvalid:
		return "PANEL_UI.CONFIGURATION_SURFACE_INVALID";
	case PanelUiErrorCode::ConfigurationVoiceBindingInvalid:
		return "PANEL_UI.CONFIGURATION_VOICE_BINDING_INVALID";
	case PanelUiErrorCode::ProtocolVersionUnsupported:
		return "PANEL_UI.INTENT_PROTOCOL_VERSION_UNSUPPORTED";
	case PanelUiErrorCode::IntentUnknown:
		return "PANEL_UI.INTENT_UNKNOWN";
	case PanelUiErrorCode::IntentDisabled:
		return "PANEL_UI.INTENT_DISABLED";
	case PanelUiErrorCode::IntentSourceNotAllowed:
		return "PANEL_UI.INTENT_SOURCE_NOT_ALLOWED";
	case PanelUiErrorCode::HostFixtureSourceDisabled:
		return "PANEL_UI.HOST_FIXTURE_SOURCE_DISABLED";
	case PanelUiErrorCode::VoiceEvidenceMissing:
		return "PANEL_UI.VOICE_EVIDENCE_MISSING";
	case PanelUiErrorCode::VoiceEvidenceUnexpected:
		return "PANEL_UI.VOICE_EVIDENCE_UNEXPECTED";
	case PanelUiErrorCode::VoiceEvidenceBindingMismatch:
		return "PANEL_UI.VOICE_EVIDENCE_BINDING_MISMATCH";
	case PanelUiErrorCode::VoiceSpokenFormUnknown:
		return "PANEL_UI.VOICE_SPOKEN_FORM_UNKNOWN";
	case PanelUiErrorCode::VoiceSpokenFormIntentMismatch:
		return "PANEL_UI.VOICE_SPOKEN_FORM_INTENT_MISMATCH";
	case PanelUiErrorCode::SourceInstanceInvalid:
		return "PANEL_UI.SOURCE_INSTANCE_INVALID";
	case PanelUiErrorCode::SourceSequenceNonmonotonic:
		return "PANEL_UI.SOURCE_SEQUENCE_NONMONOTONIC";
	case PanelUiErrorCode::SourceInstanceCapacityExceeded:
		return "PANEL_UI.SOURCE_INSTANCE_CAPACITY_EXCEEDED";
	case PanelUiErrorCode::ActiveContextMismatch:
		return "PANEL_UI.ACTIVE_CONTEXT_MISMATCH";
	case PanelUiErrorCode::RenderGenerationMismatch:
		return "PANEL_UI.RENDER_GENERATION_MISMATCH";
	case PanelUiErrorCode::SurfaceInactive:
		return "PANEL_UI.SURFACE_INACTIVE";
	case PanelUiErrorCode::SurfaceIntentUnsupported:
		return "PANEL_UI.SURFACE_INTENT_UNSUPPORTED";
	case PanelUiErrorCode::LifecycleInputDisabled:
		return "PANEL_UI.LIFECYCLE_INPUT_DISABLED";
	case PanelUiErrorCode::TransitionInputBlocked:
		return "PANEL_UI.TRANSITION_INPUT_BLOCKED";
	case PanelUiErrorCode::IngressSequenceExhausted:
		return "PANEL_UI.INGRESS_SEQUENCE_EXHAUSTED";
	case PanelUiErrorCode::IngressClockRegressed:
		return "PANEL_UI.INGRESS_CLOCK_REGRESSED";
	case PanelUiErrorCode::IntentDuplicate:
		return "PANEL_UI.INTENT_DUPLICATE";
	case PanelUiErrorCode::IntentConflict:
		return "PANEL_UI.INTENT_CONFLICT";
	case PanelUiErrorCode::UnicodeConfigurationInvalid:
		return "PANEL_UI.UNICODE_CONFIGURATION_INVALID";
	case PanelUiErrorCode::UnicodeInvalidUtf8:
		return "PANEL_UI.UNICODE_INVALID_UTF8";
	case PanelUiErrorCode::UnicodeByteLimitExceeded:
		return "PANEL_UI.UNICODE_BYTE_LIMIT_EXCEEDED";
	case PanelUiErrorCode::UnicodeCodePointLimitExceeded:
		return "PANEL_UI.UNICODE_CODE_POINT_LIMIT_EXCEEDED";
	case PanelUiErrorCode::UnicodeControlCharacterForbidden:
		return "PANEL_UI.UNICODE_CONTROL_CHARACTER_FORBIDDEN";
	case PanelUiErrorCode::RendererGenerationExhausted:
		return "PANEL_UI.RENDERER_GENERATION_EXHAUSTED";
	case PanelUiErrorCode::LifecycleSocketConfigurationInvalid:
		return "PANEL_UI.LIFECYCLE_SOCKET_CONFIGURATION_INVALID";
	case PanelUiErrorCode::LifecycleSocketCreateFailed:
		return "PANEL_UI.LIFECYCLE_SOCKET_CREATE_FAILED";
	case PanelUiErrorCode::LifecycleSocketConnectFailed:
		return "PANEL_UI.LIFECYCLE_SOCKET_CONNECT_FAILED";
	case PanelUiErrorCode::LifecycleSocketPeerIdentityFailed:
		return "PANEL_UI.LIFECYCLE_SOCKET_PEER_IDENTITY_FAILED";
	case PanelUiErrorCode::LifecycleSocketPeerMismatch:
		return "PANEL_UI.LIFECYCLE_SOCKET_PEER_MISMATCH";
	case PanelUiErrorCode::LifecycleSocketReceiveTimedOut:
		return "PANEL_UI.LIFECYCLE_SOCKET_RECEIVE_TIMED_OUT";
	case PanelUiErrorCode::LifecycleSocketReceiveFailed:
		return "PANEL_UI.LIFECYCLE_SOCKET_RECEIVE_FAILED";
	case PanelUiErrorCode::LifecycleSocketDisconnected:
		return "PANEL_UI.LIFECYCLE_SOCKET_DISCONNECTED";
	case PanelUiErrorCode::LifecycleSocketSendFailed:
		return "PANEL_UI.LIFECYCLE_SOCKET_SEND_FAILED";
	case PanelUiErrorCode::LifecycleProtocolInvalid:
		return "PANEL_UI.LIFECYCLE_PROTOCOL_INVALID";
	case PanelUiErrorCode::LifecycleReleaseMismatch:
		return "PANEL_UI.LIFECYCLE_RELEASE_MISMATCH";
	case PanelUiErrorCode::LifecycleSessionMismatch:
		return "PANEL_UI.LIFECYCLE_SESSION_MISMATCH";
	case PanelUiErrorCode::LifecycleChallengeMismatch:
		return "PANEL_UI.LIFECYCLE_CHALLENGE_MISMATCH";
	case PanelUiErrorCode::LifecycleSequenceNonmonotonic:
		return "PANEL_UI.LIFECYCLE_SEQUENCE_NONMONOTONIC";
	case PanelUiErrorCode::LifecycleRenderFailed:
		return "PANEL_UI.LIFECYCLE_RENDER_FAILED";
	case PanelUiErrorCode::LifecycleRenderGenerationNonmonotonic:
		return "PANEL_UI.LIFECYCLE_RENDER_GENERATION_NONMONOTONIC";
	case PanelUiErrorCode::LifecycleAcknowledgementEncodeFailed:
		return "PANEL_UI.LIFECYCLE_ACKNOWLEDGEMENT_ENCODE_FAILED";
	case PanelUiErrorCode::LifecycleDisplayCommitBackendUnavailable:
		return "PANEL_UI.LIFECYCLE_DISPLAY_COMMIT_BACKEND_UNAVAILABLE";
	case PanelUiErrorCode::LifecycleDisplayCommitEvidenceInvalid:
		return "PANEL_UI.LIFECYCLE_DISPLAY_COMMIT_EVIDENCE_INVALID";
	case PanelUiErrorCode::LifecycleServiceConfigurationInvalid:
		return "PANEL_UI.LIFECYCLE_SERVICE_CONFIGURATION_INVALID";
	case PanelUiErrorCode::LifecycleServiceConnectorFailed:
		return "PANEL_UI.LIFECYCLE_SERVICE_CONNECTOR_FAILED";
	case PanelUiErrorCode::LifecycleServiceDeadlineOverflow:
		return "PANEL_UI.LIFECYCLE_SERVICE_DEADLINE_OVERFLOW";
	case PanelUiErrorCode::LifecycleServiceDiagnosticFailed:
		return "PANEL_UI.LIFECYCLE_SERVICE_DIAGNOSTIC_FAILED";
	case PanelUiErrorCode::LifecycleServiceFailureLimitReached:
		return "PANEL_UI.LIFECYCLE_SERVICE_FAILURE_LIMIT_REACHED";
	case PanelUiErrorCode::LifecycleServiceSignalSetupFailed:
		return "PANEL_UI.LIFECYCLE_SERVICE_SIGNAL_SETUP_FAILED";
	case PanelUiErrorCode::LifecycleServiceWaitFailed:
		return "PANEL_UI.LIFECYCLE_SERVICE_WAIT_FAILED";
	}
	return "PANEL_UI.UNRECOGNIZED_ERROR";
}

PanelUiError::PanelUiError(const PanelUiErrorCode code, std::string message,
			   std::map<std::string, std::string> context)
	: code_(code), message_(std::move(message)), context_(std::move(context))
{
}

PanelUiErrorCode PanelUiError::code() const noexcept { return code_; }
std::string_view PanelUiError::code_string() const noexcept { return to_string(code_); }
const std::string &PanelUiError::message() const noexcept { return message_; }
const std::map<std::string, std::string> &PanelUiError::context() const noexcept
{
	return context_;
}

} // namespace house_panel::panel_ui
