#include "panel_supervisor/offline_solar_calculator.hpp"

#include "panel_supervisor/error_code.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <map>
#include <string>

namespace house_panel::supervisor {
namespace {

constexpr double kPi = 3.141592653589793238462643383279502884;
constexpr double kSunriseSunsetZenithDegrees = 90.833;
constexpr int kFirstDocumentedAccurateYear = 1800;
constexpr int kLastDocumentedAccurateYear = 2100;

double degrees_to_radians(const double degrees) noexcept { return degrees * kPi / 180.0; }

double radians_to_degrees(const double radians) noexcept { return radians * 180.0 / kPi; }

bool is_gregorian_leap_year(const int year) noexcept
{
	return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

Result<std::tm> utc_calendar_time(const std::chrono::system_clock::time_point observed_at)
{
	const std::time_t observed_time = std::chrono::system_clock::to_time_t(observed_at);
	std::tm utc_time{};
#if defined(_WIN32)
	if (gmtime_s(&utc_time, &observed_time) != 0) {
#else
	if (gmtime_r(&observed_time, &utc_time) == nullptr) {
#endif
		return Result<std::tm>::failure(SupervisorError(
			ErrorCode::SolarCalculationInvalid,
			"Failed to convert the supervisor clock instant to a UTC calendar time",
			{{"operation", "offline_solar_calculator.utc_calendar_time"}}));
	}
	return Result<std::tm>::success(utc_time);
}

} // namespace

Result<OfflineSolarCalculator> OfflineSolarCalculator::create(const double latitude_degrees,
							      const double longitude_degrees)
{
	std::map<std::string, std::string> invalid_fields;
	if (!std::isfinite(latitude_degrees) || latitude_degrees < -90.0 ||
	    latitude_degrees > 90.0) {
		invalid_fields.emplace("latitude_degrees", "must_be_finite_and_between_-90_and_90");
	}
	if (!std::isfinite(longitude_degrees) || longitude_degrees < -180.0 ||
	    longitude_degrees > 180.0) {
		invalid_fields.emplace("longitude_degrees",
				       "must_be_finite_and_between_-180_and_180");
	}
	if (!invalid_fields.empty()) {
		return Result<OfflineSolarCalculator>::failure(SupervisorError(
			ErrorCode::SolarConfigurationInvalid,
			"Failed to create offline solar calculator because coordinates are invalid",
			std::move(invalid_fields)));
	}
	return Result<OfflineSolarCalculator>::success(
		OfflineSolarCalculator(latitude_degrees, longitude_degrees));
}

OfflineSolarCalculator::OfflineSolarCalculator(const double latitude_degrees,
					       const double longitude_degrees) noexcept
    : latitude_degrees_(latitude_degrees), longitude_degrees_(longitude_degrees)
{
}

Result<SolarState>
OfflineSolarCalculator::calculate(const std::chrono::system_clock::time_point observed_at)
{
	const auto elevation_result = solar_elevation_degrees(observed_at);
	if (!elevation_result) {
		return Result<SolarState>::failure(elevation_result.error());
	}
	const double sunrise_sunset_elevation_degrees = 90.0 - kSunriseSunsetZenithDegrees;
	return Result<SolarState>::success(
		elevation_result.value() >= sunrise_sunset_elevation_degrees ? SolarState::Day
									     : SolarState::Night);
}

Result<double> OfflineSolarCalculator::solar_elevation_degrees(
	const std::chrono::system_clock::time_point observed_at) const
{
	const auto utc_result = utc_calendar_time(observed_at);
	if (!utc_result) {
		return Result<double>::failure(utc_result.error());
	}
	const std::tm &utc_time = utc_result.value();
	const int year = utc_time.tm_year + 1900;
	if (year < kFirstDocumentedAccurateYear || year > kLastDocumentedAccurateYear) {
		return Result<double>::failure(SupervisorError(
			ErrorCode::SolarCalculationInvalid,
			"Failed to calculate solar state because UTC year is outside the "
			"documented equation range",
			{{"maximum_year", std::to_string(kLastDocumentedAccurateYear)},
			 {"minimum_year", std::to_string(kFirstDocumentedAccurateYear)},
			 {"observed_year", std::to_string(year)}}));
	}

	const double days_in_year = is_gregorian_leap_year(year) ? 366.0 : 365.0;
	const double fractional_hour = static_cast<double>(utc_time.tm_hour) +
				       static_cast<double>(utc_time.tm_min) / 60.0 +
				       static_cast<double>(utc_time.tm_sec) / 3600.0;
	const double fractional_year =
		2.0 * kPi / days_in_year *
		(static_cast<double>(utc_time.tm_yday) + (fractional_hour - 12.0) / 24.0);

	// @see https://gml.noaa.gov/grad/solcalc/solareqns.PDF
	const double equation_of_time_minutes =
		229.18 *
		(0.000075 + 0.001868 * std::cos(fractional_year) -
		 0.032077 * std::sin(fractional_year) - 0.014615 * std::cos(2.0 * fractional_year) -
		 0.040849 * std::sin(2.0 * fractional_year));
	const double solar_declination_radians = 0.006918 - 0.399912 * std::cos(fractional_year) +
						 0.070257 * std::sin(fractional_year) -
						 0.006758 * std::cos(2.0 * fractional_year) +
						 0.000907 * std::sin(2.0 * fractional_year) -
						 0.002697 * std::cos(3.0 * fractional_year) +
						 0.00148 * std::sin(3.0 * fractional_year);

	double true_solar_time_minutes =
		fractional_hour * 60.0 + equation_of_time_minutes + 4.0 * longitude_degrees_;
	true_solar_time_minutes = std::fmod(true_solar_time_minutes, 1440.0);
	if (true_solar_time_minutes < 0.0) {
		true_solar_time_minutes += 1440.0;
	}
	const double solar_hour_angle_radians =
		degrees_to_radians(true_solar_time_minutes / 4.0 - 180.0);
	const double latitude_radians = degrees_to_radians(latitude_degrees_);
	const double cosine_solar_zenith =
		std::sin(latitude_radians) * std::sin(solar_declination_radians) +
		std::cos(latitude_radians) * std::cos(solar_declination_radians) *
			std::cos(solar_hour_angle_radians);
	const double bounded_cosine_solar_zenith = std::clamp(cosine_solar_zenith, -1.0, 1.0);
	const double solar_zenith_degrees =
		radians_to_degrees(std::acos(bounded_cosine_solar_zenith));
	const double solar_elevation = 90.0 - solar_zenith_degrees;
	if (!std::isfinite(solar_elevation)) {
		return Result<double>::failure(SupervisorError(
			ErrorCode::SolarCalculationInvalid,
			"Failed to calculate a finite solar elevation from the configured "
			"coordinates and UTC instant",
			{{"operation", "offline_solar_calculator.solar_elevation_degrees"}}));
	}
	return Result<double>::success(solar_elevation);
}

double OfflineSolarCalculator::latitude_degrees() const noexcept { return latitude_degrees_; }

double OfflineSolarCalculator::longitude_degrees() const noexcept { return longitude_degrees_; }

} // namespace house_panel::supervisor
