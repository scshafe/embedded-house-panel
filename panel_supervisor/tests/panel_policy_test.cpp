#include "fixtures/supervisor_test_fixture.hpp"

#include "panel_supervisor/error_code.hpp"
#include "panel_supervisor/panel_policy.hpp"
#include "panel_supervisor/panel_state.hpp"
#include "panel_supervisor/policy_observation.hpp"
#include "panel_supervisor/presence_state.hpp"
#include "panel_supervisor/solar_state.hpp"
#include "panel_supervisor/supervisor_event.hpp"
#include "panel_supervisor/system_state.hpp"
#include "panel_supervisor/ui_mode.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string_view>
#include <utility>

namespace house_panel::supervisor {
namespace {

#define TEST_REQUIRE(condition)                                                                    \
	do {                                                                                       \
		if (!(condition)) {                                                                \
			std::cerr << __func__ << ": requirement failed: " #condition << '\n';      \
			return false;                                                              \
		}                                                                                  \
	} while (false)

bool present_day_observation_returns_active_day_outputs()
{
	const auto config = test_fixture::valid_config();
	TEST_REQUIRE(config.has_value());
	const auto result = PanelPolicy{}.evaluate(PanelState{},
						   PolicyObservation{true, SolarState::Day,
								     test_fixture::kInitialTime,
								     SupervisorEvent::Start},
						   config.value());

	TEST_REQUIRE(result.has_value());
	const auto &decision = result.value();
	TEST_REQUIRE(decision.presence_state == PresenceState::Present);
	TEST_REQUIRE(decision.solar_state == SolarState::Day);
	TEST_REQUIRE(decision.target_system_state == SystemState::Active);
	TEST_REQUIRE(decision.brightness_percent == test_fixture::kPresentDayBrightnessPercent);
	TEST_REQUIRE(decision.ui_mode == UiMode::Day);
	TEST_REQUIRE(!decision.request_suspend);
	TEST_REQUIRE(decision.last_presence_detected_at == test_fixture::kInitialTime);
	TEST_REQUIRE(decision.policy_revision == 1);
	return true;
}

bool absence_before_deadline_returns_grace_night_outputs()
{
	const auto config = test_fixture::valid_config();
	TEST_REQUIRE(config.has_value());
	PanelState previous_state;
	previous_state.system_state = SystemState::Active;
	previous_state.last_presence_detected_at = test_fixture::kInitialTime;
	previous_state.policy_revision = 7;
	const auto before_deadline = test_fixture::kInitialTime + test_fixture::kGracePeriod -
				     std::chrono::milliseconds(1);

	const auto result = PanelPolicy{}.evaluate(
		previous_state,
		PolicyObservation{false, SolarState::Night, before_deadline, SupervisorEvent::Poll},
		config.value());

	TEST_REQUIRE(result.has_value());
	const auto &decision = result.value();
	TEST_REQUIRE(decision.presence_state == PresenceState::GracePeriod);
	TEST_REQUIRE(decision.target_system_state == SystemState::Active);
	TEST_REQUIRE(decision.brightness_percent == test_fixture::kGraceNightBrightnessPercent);
	TEST_REQUIRE(decision.ui_mode == UiMode::Night);
	TEST_REQUIRE(!decision.request_suspend);
	TEST_REQUIRE(decision.policy_revision == 8);
	return true;
}

bool absence_at_deadline_returns_suspended_outputs()
{
	const auto config = test_fixture::valid_config();
	TEST_REQUIRE(config.has_value());
	PanelState previous_state;
	previous_state.system_state = SystemState::Active;
	previous_state.last_presence_detected_at = test_fixture::kInitialTime;

	const auto result = PanelPolicy{}.evaluate(
		previous_state,
		PolicyObservation{false, SolarState::Day,
				  test_fixture::kInitialTime + test_fixture::kGracePeriod,
				  SupervisorEvent::Poll},
		config.value());

	TEST_REQUIRE(result.has_value());
	const auto &decision = result.value();
	TEST_REQUIRE(decision.presence_state == PresenceState::Absent);
	TEST_REQUIRE(decision.target_system_state == SystemState::Suspended);
	TEST_REQUIRE(decision.brightness_percent == test_fixture::kSuspendedBrightnessPercent);
	TEST_REQUIRE(decision.ui_mode == UiMode::Suspended);
	TEST_REQUIRE(decision.request_suspend);
	return true;
}

bool backward_clock_returns_typed_clock_error()
{
	const auto config = test_fixture::valid_config();
	TEST_REQUIRE(config.has_value());
	PanelState previous_state;
	previous_state.last_presence_detected_at = test_fixture::kInitialTime;

	const auto result = PanelPolicy{}.evaluate(
		previous_state,
		PolicyObservation{false, SolarState::Day,
				  test_fixture::kInitialTime - std::chrono::milliseconds(1),
				  SupervisorEvent::Poll},
		config.value());

	TEST_REQUIRE(!result.has_value());
	TEST_REQUIRE(result.error().code() == ErrorCode::ClockMovedBackward);
	TEST_REQUIRE(result.error().context().count("observed_at_epoch_ms") == 1);
	return true;
}

bool unknown_solar_state_returns_invalid_observation_error()
{
	const auto config = test_fixture::valid_config();
	TEST_REQUIRE(config.has_value());
	const auto result = PanelPolicy{}.evaluate(PanelState{},
						   PolicyObservation{true, SolarState::Unknown,
								     test_fixture::kInitialTime,
								     SupervisorEvent::Start},
						   config.value());

	TEST_REQUIRE(!result.has_value());
	TEST_REQUIRE(result.error().code() == ErrorCode::InvalidPolicyObservation);
	return true;
}

bool maximum_revision_returns_typed_overflow_error()
{
	const auto config = test_fixture::valid_config();
	TEST_REQUIRE(config.has_value());
	PanelState previous_state;
	previous_state.policy_revision = std::numeric_limits<std::uint64_t>::max();
	const auto result = PanelPolicy{}.evaluate(previous_state,
						   PolicyObservation{true, SolarState::Day,
								     test_fixture::kInitialTime,
								     SupervisorEvent::Poll},
						   config.value());

	TEST_REQUIRE(!result.has_value());
	TEST_REQUIRE(result.error().code() == ErrorCode::RevisionOverflow);
	return true;
}

using TestFunction = bool (*)();

} // namespace
} // namespace house_panel::supervisor

int main()
{
	using namespace house_panel::supervisor;
	const std::array<std::pair<std::string_view, TestFunction>, 6> tests{{
		{"present_day_observation_returns_active_day_outputs",
		 present_day_observation_returns_active_day_outputs},
		{"absence_before_deadline_returns_grace_night_outputs",
		 absence_before_deadline_returns_grace_night_outputs},
		{"absence_at_deadline_returns_suspended_outputs",
		 absence_at_deadline_returns_suspended_outputs},
		{"backward_clock_returns_typed_clock_error",
		 backward_clock_returns_typed_clock_error},
		{"unknown_solar_state_returns_invalid_observation_error",
		 unknown_solar_state_returns_invalid_observation_error},
		{"maximum_revision_returns_typed_overflow_error",
		 maximum_revision_returns_typed_overflow_error},
	}};

	for (const auto &[test_name, test_function] : tests) {
		if (!test_function()) {
			std::cerr << "FAILED: " << test_name << '\n';
			return 1;
		}
	}
	std::cout << "PASS: " << tests.size() << " panel policy tests\n";
	return 0;
}
