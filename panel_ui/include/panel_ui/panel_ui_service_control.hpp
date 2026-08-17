#pragma once

#include "panel_ui/result.hpp"

#include <chrono>

namespace house_panel::panel_ui {

// @extension-point panel_ui_service_control -- production signals or deterministic tests own
// stop observation and bounded reconnect waiting.
class PanelUiServiceControl {
public:
	virtual ~PanelUiServiceControl() = default;
	[[nodiscard]] virtual bool stop_requested() const noexcept = 0;

	/** Returns true after the full delay or false when stop interrupted the wait. */
	[[nodiscard]] virtual PanelUiResult<bool>
	wait_for(std::chrono::milliseconds delay) = 0;
};

} // namespace house_panel::panel_ui
