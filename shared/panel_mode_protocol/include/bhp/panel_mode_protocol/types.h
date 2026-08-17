#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace bhp::panel_mode_protocol {

inline constexpr std::uint8_t kProtocolVersion = 2;
inline constexpr std::size_t kMaximumFrameBytes = 4096;
inline constexpr std::uint64_t kMaximumSafeJsonInteger = 9'007'199'254'740'991ULL;
inline constexpr std::int64_t kMinimumObservedAtUnixMilliseconds = -5'364'662'400'000LL;
inline constexpr std::int64_t kMaximumObservedAtUnixMillisecondsExclusive =
	4'133'980'800'000LL;

enum class DisplayMode : std::uint8_t {
	DayActive = 0x01,
	NightRed = 0x02,
	Suspended = 0x03,
	Diagnostic = 0x04,
};

enum class PresenceState : std::uint8_t {
	Unknown = 0x00,
	Present = 0x01,
	GracePeriod = 0x02,
	Absent = 0x03,
};

enum class SolarState : std::uint8_t {
	Unknown = 0x00,
	Day = 0x01,
	Night = 0x02,
};

enum class SystemState : std::uint8_t {
	Starting = 0x00,
	Active = 0x01,
	Suspended = 0x02,
	RecomputingAfterResume = 0x03,
	Faulted = 0x04,
};

enum class PresentationResult : std::uint8_t {
	Committed,
	Failed,
};

[[nodiscard]] constexpr std::string_view to_string(const DisplayMode value) noexcept
{
	switch (value) {
	case DisplayMode::DayActive:
		return "day_active";
	case DisplayMode::NightRed:
		return "night_red";
	case DisplayMode::Suspended:
		return "suspended";
	case DisplayMode::Diagnostic:
		return "diagnostic";
	}
	return "unknown";
}

[[nodiscard]] constexpr std::string_view to_string(const PresenceState value) noexcept
{
	switch (value) {
	case PresenceState::Unknown:
		return "unknown";
	case PresenceState::Present:
		return "present";
	case PresenceState::GracePeriod:
		return "grace_period";
	case PresenceState::Absent:
		return "absent";
	}
	return "unknown";
}

[[nodiscard]] constexpr std::string_view to_string(const SolarState value) noexcept
{
	switch (value) {
	case SolarState::Unknown:
		return "unknown";
	case SolarState::Day:
		return "day";
	case SolarState::Night:
		return "night";
	}
	return "unknown";
}

[[nodiscard]] constexpr std::string_view to_string(const SystemState value) noexcept
{
	switch (value) {
	case SystemState::Starting:
		return "starting";
	case SystemState::Active:
		return "active";
	case SystemState::Suspended:
		return "suspended";
	case SystemState::RecomputingAfterResume:
		return "recomputing_after_resume";
	case SystemState::Faulted:
		return "faulted";
	}
	return "unknown";
}

[[nodiscard]] constexpr std::string_view to_string(const PresentationResult value) noexcept
{
	switch (value) {
	case PresentationResult::Committed:
		return "committed";
	case PresentationResult::Failed:
		return "failed";
	}
	return "failed";
}

struct Snapshot {
	std::string appliance_release_id;
	std::string supervisor_session_id;
	std::string connection_challenge;
	std::uint64_t event_sequence;
	std::string snapshot_id;
	DisplayMode display_mode;
	PresenceState presence_state;
	SolarState solar_state;
	SystemState system_state;
	std::uint8_t effective_brightness_percent;
	std::int64_t observed_at_unix_milliseconds;
};

struct PresentationAcknowledgement {
	std::string appliance_release_id;
	std::string supervisor_session_id;
	std::string connection_challenge;
	std::uint64_t event_sequence;
	std::string snapshot_id;
	DisplayMode display_mode;
	std::uint64_t render_generation;
	PresentationResult presentation_result;
};

} // namespace bhp::panel_mode_protocol
