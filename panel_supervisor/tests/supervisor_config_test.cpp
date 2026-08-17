#include "fixtures/supervisor_test_fixture.hpp"

#include "panel_supervisor/brightness_levels.hpp"
#include "panel_supervisor/error_code.hpp"
#include "panel_supervisor/supervisor_config.hpp"

#include <array>
#include <chrono>
#include <iostream>
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

bool valid_configuration_preserves_every_explicit_output()
{
	const auto result = test_fixture::valid_config();

	TEST_REQUIRE(result.has_value());
	TEST_REQUIRE(result.value().grace_period() == test_fixture::kGracePeriod);
	TEST_REQUIRE(result.value().brightness_levels().present_day_percent ==
		     test_fixture::kPresentDayBrightnessPercent);
	TEST_REQUIRE(result.value().brightness_levels().present_night_percent ==
		     test_fixture::kPresentNightBrightnessPercent);
	TEST_REQUIRE(result.value().brightness_levels().grace_day_percent ==
		     test_fixture::kGraceDayBrightnessPercent);
	TEST_REQUIRE(result.value().brightness_levels().grace_night_percent ==
		     test_fixture::kGraceNightBrightnessPercent);
	TEST_REQUIRE(result.value().brightness_levels().suspended_percent ==
		     test_fixture::kSuspendedBrightnessPercent);
	TEST_REQUIRE(result.value().presentation_timeout() ==
		     test_fixture::kPresentationTimeout);
	return true;
}

bool invalid_configuration_reports_all_invalid_fields()
{
	const auto result = SupervisorConfig::create(std::chrono::milliseconds(-1),
						     BrightnessLevels{-1, 101, 50, 20, 999},
						     std::chrono::milliseconds(0));

	TEST_REQUIRE(!result.has_value());
	TEST_REQUIRE(result.error().code() == ErrorCode::InvalidConfiguration);
	TEST_REQUIRE(result.error().context().count("grace_period_ms") == 1);
	TEST_REQUIRE(result.error().context().count("present_day_percent") == 1);
	TEST_REQUIRE(result.error().context().count("present_night_percent") == 1);
	TEST_REQUIRE(result.error().context().count("suspended_percent") == 1);
	TEST_REQUIRE(result.error().context().count("presentation_timeout_ms") == 1);
	TEST_REQUIRE(result.error().context().count("grace_day_percent") == 0);
	TEST_REQUIRE(result.error().context().count("grace_night_percent") == 0);
	return true;
}

bool suspended_brightness_must_be_exactly_zero()
{
	const auto result = SupervisorConfig::create(
		test_fixture::kGracePeriod,
		BrightnessLevels{test_fixture::kPresentDayBrightnessPercent,
				 test_fixture::kPresentNightBrightnessPercent,
				 test_fixture::kGraceDayBrightnessPercent,
				 test_fixture::kGraceNightBrightnessPercent, 1},
		test_fixture::kPresentationTimeout);

	TEST_REQUIRE(!result.has_value());
	TEST_REQUIRE(result.error().code() == ErrorCode::InvalidConfiguration);
	TEST_REQUIRE(result.error().context().at("suspended_percent") ==
		     "must_be_exactly_0_for_boot_and_suspend_safety");
	return true;
}

using TestFunction = bool (*)();

} // namespace
} // namespace house_panel::supervisor

int main()
{
	using namespace house_panel::supervisor;
	const std::array<std::pair<std::string_view, TestFunction>, 3> tests{{
		{"valid_configuration_preserves_every_explicit_output",
		 valid_configuration_preserves_every_explicit_output},
		{"invalid_configuration_reports_all_invalid_fields",
		 invalid_configuration_reports_all_invalid_fields},
		{"suspended_brightness_must_be_exactly_zero",
		 suspended_brightness_must_be_exactly_zero},
	}};

	for (const auto &[test_name, test_function] : tests) {
		if (!test_function()) {
			std::cerr << "FAILED: " << test_name << '\n';
			return 1;
		}
	}
	std::cout << "PASS: " << tests.size() << " supervisor configuration tests\n";
	return 0;
}
