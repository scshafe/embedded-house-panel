#include "panel_supervisor/error_code.hpp"
#include "panel_supervisor/panel_supervisord_config.hpp"

#include <array>
#include <chrono>
#include <iostream>
#include <map>
#include <string>
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

std::map<std::string, std::string> complete_configuration()
{
	return {
		{"BHP_PANEL_SUPERVISOR_LATITUDE", "47.6062"},
		{"BHP_PANEL_SUPERVISOR_LONGITUDE", "-122.3321"},
		{"BHP_PANEL_SUPERVISOR_PRESENCE_GRACE_SECONDS", "15"},
		{"BHP_PANEL_SUPERVISOR_PRESENCE_INPUT_PATH", "/dev/input/event7"},
		{"BHP_PANEL_SUPERVISOR_BACKLIGHT_PATH", "/sys/class/backlight/panel/brightness"},
		{"BHP_PANEL_SUPERVISOR_BACKLIGHT_MAX_PATH",
		 "/sys/class/backlight/panel/max_brightness"},
		{"BHP_PANEL_SUPERVISOR_MODE_SOCKET", "/run/beaglebone-house-panel/mode.sock"},
		{"BHP_PANEL_SUPERVISOR_DAY_BRIGHTNESS", "88"},
		{"BHP_PANEL_SUPERVISOR_NIGHT_BRIGHTNESS", "24"},
		{"BHP_PANEL_SUPERVISOR_GRACE_DAY_BRIGHTNESS", "48"},
		{"BHP_PANEL_SUPERVISOR_GRACE_NIGHT_BRIGHTNESS", "12"},
		{"BHP_PANEL_SUPERVISOR_OFF_BRIGHTNESS", "0"},
		{"BHP_PANEL_SUPERVISOR_SUSPEND_MODE", "disabled"},
		{"BHP_PANEL_SUPERVISOR_POLL_INTERVAL_MILLISECONDS", "250"},
	};
}

bool complete_configuration_populates_every_runtime_value()
{
	const auto result = PanelSupervisordConfig::create(complete_configuration());
	TEST_REQUIRE(result.has_value());
	const auto &config = result.value();
	TEST_REQUIRE(config.latitude_degrees() == 47.6062);
	TEST_REQUIRE(config.longitude_degrees() == -122.3321);
	TEST_REQUIRE(config.presence_input_path() == "/dev/input/event7");
	TEST_REQUIRE(config.presence_input_name().empty());
	TEST_REQUIRE(config.backlight_path() == "/sys/class/backlight/panel/brightness");
	TEST_REQUIRE(config.backlight_maximum_path() ==
		     "/sys/class/backlight/panel/max_brightness");
	TEST_REQUIRE(config.mode_socket_path() == "/run/beaglebone-house-panel/mode.sock");
	TEST_REQUIRE(config.suspend_mode() == SuspendMode::Disabled);
	TEST_REQUIRE(config.power_state_path().empty());
	TEST_REQUIRE(config.poll_interval() == std::chrono::milliseconds(250));
	TEST_REQUIRE(config.suspend_retry_backoff() == std::chrono::milliseconds(0));
	TEST_REQUIRE(config.supervisor_config().grace_period() == std::chrono::seconds(15));
	const auto &brightness = config.supervisor_config().brightness_levels();
	TEST_REQUIRE(brightness.present_day_percent == 88);
	TEST_REQUIRE(brightness.present_night_percent == 24);
	TEST_REQUIRE(brightness.grace_day_percent == 48);
	TEST_REQUIRE(brightness.grace_night_percent == 12);
	TEST_REQUIRE(brightness.suspended_percent == 0);
	return true;
}

bool missing_configuration_reports_all_required_fields_before_action()
{
	const auto result = PanelSupervisordConfig::create({});
	TEST_REQUIRE(!result.has_value());
	TEST_REQUIRE(result.error().code() == ErrorCode::DaemonConfigurationInvalid);
	const auto &context = result.error().context();
	TEST_REQUIRE(context.count("BHP_PANEL_SUPERVISOR_LATITUDE") == 1);
	TEST_REQUIRE(context.count("BHP_PANEL_SUPERVISOR_LONGITUDE") == 1);
	TEST_REQUIRE(context.count("BHP_PANEL_SUPERVISOR_PRESENCE_INPUT_PATH") == 1);
	TEST_REQUIRE(context.count("BHP_PANEL_SUPERVISOR_PRESENCE_INPUT_NAME") == 1);
	TEST_REQUIRE(context.count("BHP_PANEL_SUPERVISOR_BACKLIGHT_PATH") == 1);
	TEST_REQUIRE(context.count("BHP_PANEL_SUPERVISOR_BACKLIGHT_MAX_PATH") == 1);
	TEST_REQUIRE(context.count("BHP_PANEL_SUPERVISOR_MODE_SOCKET") == 1);
	TEST_REQUIRE(context.count("BHP_PANEL_SUPERVISOR_SUSPEND_MODE") == 1);
	TEST_REQUIRE(context.count("BHP_PANEL_SUPERVISOR_POLL_INTERVAL_MILLISECONDS") == 1);
	return true;
}

bool enabled_suspend_requires_an_explicit_power_state_path()
{
	auto fields = complete_configuration();
	fields["BHP_PANEL_SUPERVISOR_SUSPEND_MODE"] = "freeze";
	const auto missing_path_result = PanelSupervisordConfig::create(fields);
	TEST_REQUIRE(!missing_path_result.has_value());
	TEST_REQUIRE(missing_path_result.error().code() == ErrorCode::DaemonConfigurationInvalid);
	TEST_REQUIRE(missing_path_result.error().context().count(
			     "BHP_PANEL_SUPERVISOR_POWER_STATE_PATH") == 1);
	TEST_REQUIRE(missing_path_result.error().context().count(
			     "BHP_PANEL_SUPERVISOR_SUSPEND_RETRY_BACKOFF_MILLISECONDS") == 1);

	fields["BHP_PANEL_SUPERVISOR_POWER_STATE_PATH"] = "/sys/power/state";
	fields["BHP_PANEL_SUPERVISOR_SUSPEND_RETRY_BACKOFF_MILLISECONDS"] = "5000";
	const auto enabled_result = PanelSupervisordConfig::create(fields);
	TEST_REQUIRE(enabled_result.has_value());
	TEST_REQUIRE(enabled_result.value().suspend_mode() == SuspendMode::Freeze);
	TEST_REQUIRE(enabled_result.value().power_state_path() == "/sys/power/state");
	TEST_REQUIRE(enabled_result.value().suspend_retry_backoff() ==
		     std::chrono::milliseconds(5000));
	return true;
}

bool exact_name_presence_selection_is_mutually_exclusive_with_path()
{
	auto fields = complete_configuration();
	fields.erase("BHP_PANEL_SUPERVISOR_PRESENCE_INPUT_PATH");
	fields["BHP_PANEL_SUPERVISOR_PRESENCE_INPUT_NAME"] = "verified-front-proximity";
	const auto name_result = PanelSupervisordConfig::create(fields);
	TEST_REQUIRE(name_result.has_value());
	TEST_REQUIRE(name_result.value().presence_input_path().empty());
	TEST_REQUIRE(name_result.value().presence_input_name() == "verified-front-proximity");

	fields["BHP_PANEL_SUPERVISOR_PRESENCE_INPUT_PATH"] = "/dev/input/event7";
	const auto both_result = PanelSupervisordConfig::create(fields);
	TEST_REQUIRE(!both_result.has_value());
	TEST_REQUIRE(both_result.error().context().count(
			     "BHP_PANEL_SUPERVISOR_PRESENCE_INPUT_PATH") == 1);
	TEST_REQUIRE(both_result.error().context().count(
			     "BHP_PANEL_SUPERVISOR_PRESENCE_INPUT_NAME") == 1);
	return true;
}

bool polling_and_suspend_backoff_are_independently_bounded()
{
	auto fields = complete_configuration();
	fields["BHP_PANEL_SUPERVISOR_POLL_INTERVAL_MILLISECONDS"] = "49";
	const auto short_poll_result = PanelSupervisordConfig::create(fields);
	TEST_REQUIRE(!short_poll_result.has_value());
	TEST_REQUIRE(short_poll_result.error().context().count(
			     "BHP_PANEL_SUPERVISOR_POLL_INTERVAL_MILLISECONDS") == 1);

	fields["BHP_PANEL_SUPERVISOR_POLL_INTERVAL_MILLISECONDS"] = "5001";
	const auto long_poll_result = PanelSupervisordConfig::create(fields);
	TEST_REQUIRE(!long_poll_result.has_value());

	fields["BHP_PANEL_SUPERVISOR_POLL_INTERVAL_MILLISECONDS"] = "250";
	fields["BHP_PANEL_SUPERVISOR_SUSPEND_MODE"] = "mem";
	fields["BHP_PANEL_SUPERVISOR_POWER_STATE_PATH"] = "/sys/power/state";
	fields["BHP_PANEL_SUPERVISOR_SUSPEND_RETRY_BACKOFF_MILLISECONDS"] = "999";
	const auto short_backoff_result = PanelSupervisordConfig::create(fields);
	TEST_REQUIRE(!short_backoff_result.has_value());
	TEST_REQUIRE(short_backoff_result.error().context().count(
			     "BHP_PANEL_SUPERVISOR_SUSPEND_RETRY_BACKOFF_MILLISECONDS") == 1);
	return true;
}

bool off_brightness_must_be_exactly_zero()
{
	auto fields = complete_configuration();
	fields["BHP_PANEL_SUPERVISOR_OFF_BRIGHTNESS"] = "1";
	const auto result = PanelSupervisordConfig::create(fields);
	TEST_REQUIRE(!result.has_value());
	TEST_REQUIRE(result.error().code() == ErrorCode::DaemonConfigurationInvalid);
	TEST_REQUIRE(result.error().context().at("BHP_PANEL_SUPERVISOR_OFF_BRIGHTNESS") ==
		     "must_be_exactly_0_for_boot_and_suspend_safety");
	return true;
}

bool malformed_values_are_aggregated_without_logging_location_values()
{
	auto fields = complete_configuration();
	const std::string private_location_text = "47.6062-not-a-number";
	fields["BHP_PANEL_SUPERVISOR_LATITUDE"] = private_location_text;
	fields["BHP_PANEL_SUPERVISOR_DAY_BRIGHTNESS"] = "101";
	fields["BHP_PANEL_SUPERVISOR_POLL_INTERVAL_MILLISECONDS"] = "0";
	fields["BHP_PANEL_SUPERVISOR_BACKLIGHT_PATH"] = "REPLACE_ME/brightness";
	const auto result = PanelSupervisordConfig::create(fields);
	TEST_REQUIRE(!result.has_value());
	TEST_REQUIRE(result.error().code() == ErrorCode::DaemonConfigurationInvalid);
	const auto &context = result.error().context();
	TEST_REQUIRE(context.count("BHP_PANEL_SUPERVISOR_LATITUDE") == 1);
	TEST_REQUIRE(context.count("BHP_PANEL_SUPERVISOR_DAY_BRIGHTNESS") == 1);
	TEST_REQUIRE(context.count("BHP_PANEL_SUPERVISOR_POLL_INTERVAL_MILLISECONDS") == 1);
	TEST_REQUIRE(context.count("BHP_PANEL_SUPERVISOR_BACKLIGHT_PATH") == 1);
	for (const auto &[key, value] : context) {
		(void)key;
		TEST_REQUIRE(value.find(private_location_text) == std::string::npos);
	}
	return true;
}

using TestFunction = bool (*)();

} // namespace
} // namespace house_panel::supervisor

int main()
{
	using namespace house_panel::supervisor;
	const std::array<std::pair<std::string_view, TestFunction>, 7> tests{{
		{"complete configuration", complete_configuration_populates_every_runtime_value},
		{"missing configuration",
		 missing_configuration_reports_all_required_fields_before_action},
		{"enabled suspend path", enabled_suspend_requires_an_explicit_power_state_path},
		{"presence selector",
		 exact_name_presence_selection_is_mutually_exclusive_with_path},
		{"bounded cadences", polling_and_suspend_backoff_are_independently_bounded},
		{"off brightness", off_brightness_must_be_exactly_zero},
		{"malformed aggregation",
		 malformed_values_are_aggregated_without_logging_location_values},
	}};
	for (const auto &[name, test] : tests) {
		if (!test()) {
			std::cerr << "FAILED: " << name << '\n';
			return 1;
		}
	}
	std::cout << "panel_supervisord_config_test: " << tests.size() << " passed\n";
	return 0;
}
