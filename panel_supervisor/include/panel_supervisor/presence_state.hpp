#pragma once

#include <string_view>

namespace house_panel::supervisor {

// @canonical panel_presence_state -- policy-owned interpretation of a raw presence reading.
enum class PresenceState {
	Unknown,
	Present,
	GracePeriod,
	Absent,
};

/** Returns the stable snake_case representation used in logs and diagnostics. */
[[nodiscard]] std::string_view to_string(PresenceState presence_state) noexcept;

} // namespace house_panel::supervisor
