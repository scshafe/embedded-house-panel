#pragma once

#include <string_view>

namespace house_panel::supervisor {

// @canonical panel_supervisor_error_codes -- machine-readable failure taxonomy for this domain.
enum class ErrorCode {
	InvalidConfiguration,
	InvalidPolicyObservation,
	InvalidTraceId,
	ClockReadFailed,
	PresenceReadFailed,
	SolarCalculationFailed,
	BacklightWriteFailed,
	UiModeNotificationFailed,
	SuspendRequestFailed,
	StructuredLogWriteFailed,
	ErrorReportingFailed,
	ClockMovedBackward,
	RevisionOverflow,
	AdapterUnavailable,
	PlatformUnsupported,
	DaemonConfigurationInvalid,
	DaemonSignalSetupFailed,
	DaemonStopChannelCreateFailed,
	DaemonStopChannelConfigurationFailed,
	DaemonStopControllerAlreadyActive,
	DaemonStopWaitFailed,
	SolarConfigurationInvalid,
	SolarCalculationInvalid,
	EvdevConfigurationInvalid,
	EvdevDeviceOpenFailed,
	EvdevCapabilityQueryFailed,
	EvdevCapabilityMissing,
	EvdevStateQueryFailed,
	EvdevDeviceCloseFailed,
	EvdevDiscoveryDirectoryOpenFailed,
	EvdevDiscoveryDirectoryReadFailed,
	EvdevDeviceNameQueryFailed,
	EvdevDeviceNotFound,
	EvdevDeviceAmbiguous,
	BacklightConfigurationInvalid,
	BacklightMaximumReadFailed,
	BacklightMaximumInvalid,
	BacklightDeviceWriteFailed,
	UiSocketConfigurationInvalid,
	UiSocketCreateFailed,
	UiSocketBindFailed,
	UiSocketPermissionFailed,
	UiSocketListenFailed,
	UiSocketAcceptFailed,
	UiSocketSendFailed,
	UiSocketReceiveFailed,
	UiSocketProtocolFailed,
	UiSocketPeerIdentityFailed,
	UiSocketRandomFailed,
	UiPresentationExplicitlyFailed,
	UiPresentationBindingInvalid,
	UiSocketSequenceOverflow,
	UiSocketStalePathInspectionFailed,
	UiSocketStalePathRemovalFailed,
	SuspendConfigurationInvalid,
	PowerStateWriteFailed,
	SystemClockSynchronizationQueryFailed,
	SystemClockUntrusted,
	SimulationInvalidArgument,
};

/** Returns the stable dot-delimited machine code for an ErrorCode. */
[[nodiscard]] std::string_view to_string(ErrorCode error_code) noexcept;

} // namespace house_panel::supervisor
