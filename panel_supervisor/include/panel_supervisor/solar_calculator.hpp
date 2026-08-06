#pragma once

#include "panel_supervisor/result.hpp"
#include "panel_supervisor/solar_state.hpp"

#include <chrono>

namespace house_panel::supervisor {

// @extension-point solar_calculation -- implementations may use any explicitly configured solar
// model or data source.
class SolarCalculator {
public:
	virtual ~SolarCalculator() = default;

	/**
	 * Calculates day or night for exactly `observed_at`.
	 *
	 * Location, thresholds, and external data belong to the adapter's explicit
	 * configuration. Unknown or failed calculation must return an error rather
	 * than an invented solar state.
	 */
	[[nodiscard]] virtual Result<SolarState>
	calculate(std::chrono::system_clock::time_point observed_at) = 0;
};

} // namespace house_panel::supervisor
