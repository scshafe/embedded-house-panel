#pragma once

#include <map>
#include <string>
#include <string_view>

namespace house_panel::panel_ui {

enum class PanelUiErrorCode {
	ConfigurationConflictWindowInvalid,
	ConfigurationConflictWindowEvidenceMissing,
	ConfigurationSourceInstanceBoundInvalid,
	ConfigurationSurfaceInvalid,
	ConfigurationVoiceBindingInvalid,
	ProtocolVersionUnsupported,
	IntentUnknown,
	IntentDisabled,
	IntentSourceNotAllowed,
	HostFixtureSourceDisabled,
	VoiceEvidenceMissing,
	VoiceEvidenceUnexpected,
	VoiceEvidenceBindingMismatch,
	VoiceSpokenFormUnknown,
	VoiceSpokenFormIntentMismatch,
	SourceInstanceInvalid,
	SourceSequenceNonmonotonic,
	SourceInstanceCapacityExceeded,
	ActiveContextMismatch,
	RenderGenerationMismatch,
	SurfaceInactive,
	SurfaceIntentUnsupported,
	LifecycleInputDisabled,
	TransitionInputBlocked,
	IngressSequenceExhausted,
	IngressClockRegressed,
	IntentDuplicate,
	IntentConflict,
	UnicodeConfigurationInvalid,
	UnicodeInvalidUtf8,
	UnicodeByteLimitExceeded,
	UnicodeCodePointLimitExceeded,
	UnicodeControlCharacterForbidden,
	RendererGenerationExhausted,
	LifecycleSocketConfigurationInvalid,
	LifecycleSocketCreateFailed,
	LifecycleSocketConnectFailed,
	LifecycleSocketPeerIdentityFailed,
	LifecycleSocketPeerMismatch,
	LifecycleSocketReceiveTimedOut,
	LifecycleSocketReceiveFailed,
	LifecycleSocketDisconnected,
	LifecycleSocketSendFailed,
	LifecycleProtocolInvalid,
	LifecycleReleaseMismatch,
	LifecycleSessionMismatch,
	LifecycleChallengeMismatch,
	LifecycleSequenceNonmonotonic,
	LifecycleRenderFailed,
	LifecycleRenderGenerationNonmonotonic,
	LifecycleAcknowledgementEncodeFailed,
	LifecycleDisplayCommitBackendUnavailable,
	LifecycleDisplayCommitEvidenceInvalid,
	LifecycleServiceConfigurationInvalid,
	LifecycleServiceConnectorFailed,
	LifecycleServiceDeadlineOverflow,
	LifecycleServiceDiagnosticFailed,
	LifecycleServiceFailureLimitReached,
	LifecycleServiceSignalSetupFailed,
	LifecycleServiceWaitFailed,
};

/** Returns the stable machine-readable code for one panel UI failure. */
[[nodiscard]] std::string_view to_string(PanelUiErrorCode code) noexcept;

// @canonical panel_ui_error -- every panel_ui boundary failure uses this typed shape.
class PanelUiError final {
public:
	PanelUiError(PanelUiErrorCode code, std::string message,
		     std::map<std::string, std::string> context = {});

	[[nodiscard]] PanelUiErrorCode code() const noexcept;
	[[nodiscard]] std::string_view code_string() const noexcept;
	[[nodiscard]] const std::string &message() const noexcept;
	[[nodiscard]] const std::map<std::string, std::string> &context() const noexcept;

private:
	PanelUiErrorCode code_;
	std::string message_;
	std::map<std::string, std::string> context_;
};

} // namespace house_panel::panel_ui
