#pragma once

#include "bhp/panel_mode_protocol/types.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace bhp::touch_observation_protocol {

inline constexpr std::uint32_t kTouchObservationProtocolVersion = 1;
inline constexpr std::size_t kMinimumTouchObservationIdBytes = 22;
inline constexpr std::size_t kMaximumTouchObservationIdBytes = 128;
inline constexpr std::size_t kMaximumPendingObservationCount = 1024;

enum class ContactState : std::uint8_t {
	Started = 0x01,
	Updated = 0x02,
	Completed = 0x03,
};

enum class InvalidationReason : std::uint8_t {
	NewLifecycleSnapshot = 0x01,
	NewCommittedRenderGeneration = 0x02,
	UiDisconnected = 0x03,
	Absence = 0x04,
	Night = 0x05,
	SuspendPreparation = 0x06,
	SupervisorRestart = 0x07,
	StreamBackpressure = 0x08,
};

enum class StreamState : std::uint8_t {
	AwaitingCommittedPresentation = 0x01,
	Active = 0x02,
	Disconnected = 0x03,
	Backpressured = 0x04,
};

[[nodiscard]] constexpr std::string_view to_string(const ContactState value) noexcept
{
	switch (value) {
	case ContactState::Started:
		return "contact_started";
	case ContactState::Updated:
		return "contact_updated";
	case ContactState::Completed:
		return "contact_completed";
	}
	return "unknown";
}

[[nodiscard]] constexpr std::string_view to_string(const InvalidationReason value) noexcept
{
	switch (value) {
	case InvalidationReason::NewLifecycleSnapshot:
		return "new_lifecycle_snapshot";
	case InvalidationReason::NewCommittedRenderGeneration:
		return "new_committed_render_generation";
	case InvalidationReason::UiDisconnected:
		return "ui_disconnected";
	case InvalidationReason::Absence:
		return "absence";
	case InvalidationReason::Night:
		return "night";
	case InvalidationReason::SuspendPreparation:
		return "suspend_preparation";
	case InvalidationReason::SupervisorRestart:
		return "supervisor_restart";
	case InvalidationReason::StreamBackpressure:
		return "stream_backpressure";
	}
	return "unknown";
}

[[nodiscard]] constexpr std::string_view to_string(const StreamState value) noexcept
{
	switch (value) {
	case StreamState::AwaitingCommittedPresentation:
		return "awaiting_committed_presentation";
	case StreamState::Active:
		return "active";
	case StreamState::Disconnected:
		return "disconnected";
	case StreamState::Backpressured:
		return "backpressured";
	}
	return "unknown";
}

struct LifecyclePresentationBinding {
	std::uint32_t lifecycle_protocol_version;
	std::string supervisor_session_id;
	std::uint64_t lifecycle_event_sequence;
	std::string lifecycle_snapshot_id;
	std::string ui_connection_challenge;
	std::uint64_t committed_render_generation;
};

struct NormalizedTouchObservation {
	std::uint32_t touch_observation_protocol_version;
	std::string touch_observation_id;
	std::uint64_t observation_sequence;
	LifecyclePresentationBinding presentation_binding;
	std::int64_t raw_axis_x;
	std::int64_t raw_axis_y;
	ContactState contact_state;
	std::uint64_t supervisor_measured_contact_duration_milliseconds;
};

struct BoundedStreamConfiguration {
	std::size_t maximum_pending_observation_count;
};

} // namespace bhp::touch_observation_protocol
