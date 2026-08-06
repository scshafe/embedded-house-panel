#include "panel_supervisor/panel_policy.hpp"

#include "panel_supervisor/error_code.hpp"

#include <chrono>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <string>

namespace house_panel::supervisor {
namespace {

long long epoch_milliseconds(const std::chrono::system_clock::time_point time_point)
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(time_point.time_since_epoch())
		.count();
}

} // namespace

Result<PolicyDecision> PanelPolicy::evaluate(const PanelState &previous_state,
					     const PolicyObservation &observation,
					     const SupervisorConfig &config) const
{
	if (observation.solar_state == SolarState::Unknown) {
		return Result<PolicyDecision>::failure(SupervisorError(
			ErrorCode::InvalidPolicyObservation,
			"Failed to evaluate panel policy because the observed solar state is "
			"unknown",
			{{"event", std::string(to_string(observation.event))},
			 {"solar_state", std::string(to_string(observation.solar_state))}}));
	}

	if (previous_state.policy_revision == std::numeric_limits<std::uint64_t>::max()) {
		return Result<PolicyDecision>::failure(SupervisorError(
			ErrorCode::RevisionOverflow,
			"Failed to evaluate panel policy because the policy revision is exhausted",
			{{"policy_revision", std::to_string(previous_state.policy_revision)}}));
	}

	PresenceState presence_state = PresenceState::Absent;
	std::optional<std::chrono::system_clock::time_point> last_presence_detected_at =
		previous_state.last_presence_detected_at;

	if (last_presence_detected_at.has_value() &&
	    observation.observed_at < last_presence_detected_at.value()) {
		return Result<PolicyDecision>::failure(SupervisorError(
			ErrorCode::ClockMovedBackward,
			"Failed to evaluate panel policy because observed time precedes the last "
			"presence time",
			{{"last_presence_detected_at_epoch_ms",
			  std::to_string(epoch_milliseconds(last_presence_detected_at.value()))},
			 {"observed_at_epoch_ms",
			  std::to_string(epoch_milliseconds(observation.observed_at))},
			 {"event", std::string(to_string(observation.event))}}));
	}

	if (observation.presence_detected) {
		presence_state = PresenceState::Present;
		last_presence_detected_at = observation.observed_at;
	} else if (last_presence_detected_at.has_value()) {
		const auto absence_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
			observation.observed_at - last_presence_detected_at.value());
		presence_state = absence_duration < config.grace_period()
					 ? PresenceState::GracePeriod
					 : PresenceState::Absent;
	}

	const auto &brightness = config.brightness_levels();
	int brightness_percent = brightness.suspended_percent;
	UiMode ui_mode = UiMode::Suspended;
	SystemState target_system_state = SystemState::Suspended;

	if (presence_state == PresenceState::Present ||
	    presence_state == PresenceState::GracePeriod) {
		target_system_state = SystemState::Active;
		ui_mode = observation.solar_state == SolarState::Day ? UiMode::Day : UiMode::Night;
		if (presence_state == PresenceState::Present) {
			brightness_percent = observation.solar_state == SolarState::Day
						     ? brightness.present_day_percent
						     : brightness.present_night_percent;
		} else {
			brightness_percent = observation.solar_state == SolarState::Day
						     ? brightness.grace_day_percent
						     : brightness.grace_night_percent;
		}
	}

	const bool request_suspend = target_system_state == SystemState::Suspended &&
				     (previous_state.system_state != SystemState::Suspended ||
				      observation.event == SupervisorEvent::Resume);

	return Result<PolicyDecision>::success(PolicyDecision{
		presence_state,
		observation.solar_state,
		target_system_state,
		last_presence_detected_at,
		brightness_percent,
		ui_mode,
		request_suspend,
		previous_state.policy_revision + 1,
	});
}

} // namespace house_panel::supervisor
