#pragma once

#include "panel_supervisor/result.hpp"

namespace house_panel::supervisor {

// @extension-point panel_backlight -- translate logical percent to a platform-specific output only
// inside an adapter.
class Backlight {
public:
	virtual ~Backlight() = default;

	/**
	 * Applies logical brightness in the inclusive range 0..100.
	 *
	 * The operation must be idempotent. Unsupported or failed hardware access
	 * returns a typed error; adapters must not silently clamp or ignore values.
	 */
	[[nodiscard]] virtual Result<void> set_brightness_percent(int brightness_percent) = 0;
};

} // namespace house_panel::supervisor
