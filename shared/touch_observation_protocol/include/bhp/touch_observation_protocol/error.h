#pragma once

#include <map>
#include <string>
#include <string_view>

namespace bhp::touch_observation_protocol {

enum class ErrorCode {
	ConfigurationInvalid,
	AllocationFailed,
	SynchronizationFailed,
	SynchronizationContended,
	LifecycleBindingInvalid,
	LifecycleBindingStale,
	LifecycleTransitionInvalid,
	LifecycleInactive,
	UiDisconnected,
	StreamBackpressured,
	QueueFull,
	TouchProtocolUnsupported,
	ObservationIdInvalid,
	ObservationSequenceInvalid,
	ObservationSequenceNonmonotonic,
	RawAxisInvalid,
	ContactStateInvalid,
	ContactDurationInvalid,
};

/** Returns the stable machine-readable code for one touch stream failure. */
[[nodiscard]] constexpr std::string_view to_string(const ErrorCode code) noexcept
{
	switch (code) {
	case ErrorCode::ConfigurationInvalid:
		return "TOUCH_OBSERVATION_PROTOCOL.CONFIGURATION_INVALID";
	case ErrorCode::AllocationFailed:
		return "TOUCH_OBSERVATION_PROTOCOL.ALLOCATION_FAILED";
	case ErrorCode::SynchronizationFailed:
		return "TOUCH_OBSERVATION_PROTOCOL.SYNCHRONIZATION_FAILED";
	case ErrorCode::SynchronizationContended:
		return "TOUCH_OBSERVATION_PROTOCOL.SYNCHRONIZATION_CONTENDED";
	case ErrorCode::LifecycleBindingInvalid:
		return "TOUCH_OBSERVATION_PROTOCOL.LIFECYCLE_BINDING_INVALID";
	case ErrorCode::LifecycleBindingStale:
		return "TOUCH_OBSERVATION_PROTOCOL.LIFECYCLE_BINDING_STALE";
	case ErrorCode::LifecycleTransitionInvalid:
		return "TOUCH_OBSERVATION_PROTOCOL.LIFECYCLE_TRANSITION_INVALID";
	case ErrorCode::LifecycleInactive:
		return "TOUCH_OBSERVATION_PROTOCOL.LIFECYCLE_INACTIVE";
	case ErrorCode::UiDisconnected:
		return "TOUCH_OBSERVATION_PROTOCOL.UI_DISCONNECTED";
	case ErrorCode::StreamBackpressured:
		return "TOUCH_OBSERVATION_PROTOCOL.STREAM_BACKPRESSURED";
	case ErrorCode::QueueFull:
		return "TOUCH_OBSERVATION_PROTOCOL.QUEUE_FULL";
	case ErrorCode::TouchProtocolUnsupported:
		return "TOUCH_OBSERVATION_PROTOCOL.TOUCH_PROTOCOL_UNSUPPORTED";
	case ErrorCode::ObservationIdInvalid:
		return "TOUCH_OBSERVATION_PROTOCOL.OBSERVATION_ID_INVALID";
	case ErrorCode::ObservationSequenceInvalid:
		return "TOUCH_OBSERVATION_PROTOCOL.OBSERVATION_SEQUENCE_INVALID";
	case ErrorCode::ObservationSequenceNonmonotonic:
		return "TOUCH_OBSERVATION_PROTOCOL.OBSERVATION_SEQUENCE_NONMONOTONIC";
	case ErrorCode::RawAxisInvalid:
		return "TOUCH_OBSERVATION_PROTOCOL.RAW_AXIS_INVALID";
	case ErrorCode::ContactStateInvalid:
		return "TOUCH_OBSERVATION_PROTOCOL.CONTACT_STATE_INVALID";
	case ErrorCode::ContactDurationInvalid:
		return "TOUCH_OBSERVATION_PROTOCOL.CONTACT_DURATION_INVALID";
	}
	return "TOUCH_OBSERVATION_PROTOCOL.UNKNOWN";
}

struct Error {
	ErrorCode code;
	std::string message;
	std::map<std::string, std::string> context;
};

} // namespace bhp::touch_observation_protocol
