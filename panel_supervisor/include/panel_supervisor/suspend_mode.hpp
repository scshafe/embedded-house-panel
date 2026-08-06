#pragma once

#include <string_view>

namespace house_panel::supervisor {

// @canonical panel_supervisord_suspend_mode -- complete set of explicit daemon power actions.
enum class SuspendMode {
	Disabled,
	Freeze,
	Mem,
};

/** Returns the exact sysfs/config spelling for the suspend mode. */
[[nodiscard]] std::string_view to_string(SuspendMode suspend_mode) noexcept;

} // namespace house_panel::supervisor
