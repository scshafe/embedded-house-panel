#pragma once

#include <string_view>

namespace house_panel::supervisor {

// @canonical panel_solar_state -- result of solar calculation at a specific observed time.
enum class SolarState {
	Unknown,
	Day,
	Night,
};

/** Returns the stable snake_case representation used in logs and diagnostics. */
[[nodiscard]] std::string_view to_string(SolarState solar_state) noexcept;

} // namespace house_panel::supervisor
