#pragma once

#include <string_view>

namespace house_panel::supervisor {

// @canonical panel_ui_mode -- complete UI presentation mode emitted by supervisor policy.
enum class UiMode {
	Day,
	Night,
	Suspended,
};

/** Returns the stable snake_case representation used in logs and diagnostics. */
[[nodiscard]] std::string_view to_string(UiMode ui_mode) noexcept;

} // namespace house_panel::supervisor
