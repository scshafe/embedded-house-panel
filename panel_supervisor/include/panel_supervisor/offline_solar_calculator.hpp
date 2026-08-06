#pragma once

#include "panel_supervisor/solar_calculator.hpp"

namespace house_panel::supervisor {

// @canonical offline_solar_calculation -- NOAA-equation day/night calculation with no network
// input.
class OfflineSolarCalculator final : public SolarCalculator {
public:
	/**
	 * Validates and constructs an offline calculator.
	 *
	 * Latitude is decimal degrees in [-90, 90], positive north. Longitude is
	 * decimal degrees in [-180, 180], positive east. Invalid or non-finite input
	 * returns PANEL_SUPERVISOR.SOLAR_CONFIGURATION_INVALID.
	 */
	[[nodiscard]] static Result<OfflineSolarCalculator> create(double latitude_degrees,
								   double longitude_degrees);

	/**
	 * Calculates day when solar zenith is at most 90.833 degrees, otherwise night.
	 *
	 * `observed_at` is interpreted as UTC. UTC conversion or non-finite
	 * intermediate failure returns PANEL_SUPERVISOR.SOLAR_CALCULATION_INVALID.
	 */
	[[nodiscard]] Result<SolarState>
	calculate(std::chrono::system_clock::time_point observed_at) override;

	/** Returns the uncorrected geometric solar elevation used for threshold tests. */
	[[nodiscard]] Result<double>
	solar_elevation_degrees(std::chrono::system_clock::time_point observed_at) const;

	[[nodiscard]] double latitude_degrees() const noexcept;
	[[nodiscard]] double longitude_degrees() const noexcept;

private:
	OfflineSolarCalculator(double latitude_degrees, double longitude_degrees) noexcept;

	double latitude_degrees_;
	double longitude_degrees_;
};

} // namespace house_panel::supervisor
