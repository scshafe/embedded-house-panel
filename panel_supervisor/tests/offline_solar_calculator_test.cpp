#include "panel_supervisor/error_code.hpp"
#include "panel_supervisor/offline_solar_calculator.hpp"

#include <array>
#include <chrono>
#include <cmath>
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

std::chrono::system_clock::time_point at_epoch_seconds(const long long seconds)
{
	return std::chrono::system_clock::time_point(std::chrono::seconds(seconds));
}

bool invalid_coordinates_are_rejected_without_substitution()
{
	const auto latitude_result =
		OfflineSolarCalculator::create(std::numeric_limits<double>::quiet_NaN(), 0.0);
	TEST_REQUIRE(!latitude_result.has_value());
	TEST_REQUIRE(latitude_result.error().code() == ErrorCode::SolarConfigurationInvalid);
	TEST_REQUIRE(latitude_result.error().context().count("latitude_degrees") == 1);

	const auto longitude_result = OfflineSolarCalculator::create(0.0, 180.001);
	TEST_REQUIRE(!longitude_result.has_value());
	TEST_REQUIRE(longitude_result.error().code() == ErrorCode::SolarConfigurationInvalid);
	TEST_REQUIRE(longitude_result.error().context().count("longitude_degrees") == 1);
	return true;
}

bool equinox_noon_and_midnight_use_utc_solar_elevation()
{
	auto calculator_result = OfflineSolarCalculator::create(0.0, 0.0);
	TEST_REQUIRE(calculator_result.has_value());
	auto calculator = calculator_result.value();

	const auto noon = at_epoch_seconds(1'710'936'000LL); // 2024-03-20T12:00:00Z
	const auto noon_state = calculator.calculate(noon);
	const auto noon_elevation = calculator.solar_elevation_degrees(noon);
	TEST_REQUIRE(noon_state.has_value());
	TEST_REQUIRE(noon_elevation.has_value());
	TEST_REQUIRE(noon_state.value() == SolarState::Day);
	TEST_REQUIRE(noon_elevation.value() > 85.0);

	const auto midnight = at_epoch_seconds(1'710'892'800LL); // 2024-03-20T00:00:00Z
	const auto midnight_state = calculator.calculate(midnight);
	const auto midnight_elevation = calculator.solar_elevation_degrees(midnight);
	TEST_REQUIRE(midnight_state.has_value());
	TEST_REQUIRE(midnight_elevation.has_value());
	TEST_REQUIRE(midnight_state.value() == SolarState::Night);
	TEST_REQUIRE(midnight_elevation.value() < -85.0);
	return true;
}

bool standard_sunrise_threshold_changes_london_state()
{
	auto calculator_result = OfflineSolarCalculator::create(51.5074, -0.1278);
	TEST_REQUIRE(calculator_result.has_value());
	auto calculator = calculator_result.value();

	const auto before_sunrise =
		calculator.calculate(at_epoch_seconds(1'718'940'600LL)); // 2024-06-21T03:30Z
	const auto after_sunrise =
		calculator.calculate(at_epoch_seconds(1'718'942'400LL)); // 2024-06-21T04:00Z
	TEST_REQUIRE(before_sunrise.has_value());
	TEST_REQUIRE(after_sunrise.has_value());
	TEST_REQUIRE(before_sunrise.value() == SolarState::Night);
	TEST_REQUIRE(after_sunrise.value() == SolarState::Day);
	return true;
}

bool utc_year_outside_documented_equation_range_is_visible()
{
	auto calculator_result = OfflineSolarCalculator::create(0.0, 0.0);
	TEST_REQUIRE(calculator_result.has_value());
	auto calculator = calculator_result.value();

	const auto result =
		calculator.calculate(at_epoch_seconds(7'321'344'000LL)); // 2202-01-01T00:00:00Z
	TEST_REQUIRE(!result.has_value());
	TEST_REQUIRE(result.error().code() == ErrorCode::SolarCalculationInvalid);
	TEST_REQUIRE(result.error().context().at("minimum_year") == "1800");
	TEST_REQUIRE(result.error().context().at("maximum_year") == "2100");
	return true;
}

using TestFunction = bool (*)();

} // namespace
} // namespace house_panel::supervisor

int main()
{
	using namespace house_panel::supervisor;
	const std::array<std::pair<std::string_view, TestFunction>, 4> tests{{
		{"invalid coordinates", invalid_coordinates_are_rejected_without_substitution},
		{"UTC solar elevation", equinox_noon_and_midnight_use_utc_solar_elevation},
		{"sunrise threshold", standard_sunrise_threshold_changes_london_state},
		{"documented year range", utc_year_outside_documented_equation_range_is_visible},
	}};
	for (const auto &[name, test] : tests) {
		if (!test()) {
			std::cerr << "FAILED: " << name << '\n';
			return 1;
		}
	}
	std::cout << "offline_solar_calculator_test: " << tests.size() << " passed\n";
	return 0;
}
