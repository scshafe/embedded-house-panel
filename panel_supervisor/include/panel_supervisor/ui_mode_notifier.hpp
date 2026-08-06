#pragma once

#include "panel_supervisor/result.hpp"
#include "panel_supervisor/ui_mode_notification.hpp"

namespace house_panel::supervisor {

// @extension-point ui_mode_notification -- connect policy presentation mode to any UI process or
// in-process view.
class UiModeNotifier {
public:
	virtual ~UiModeNotifier() = default;

	/**
	 * Publishes one complete, idempotent UI snapshot or returns a typed failure.
	 *
	 * The notification contains every value required by the versioned process
	 * protocol. Implementations must not infer missing policy state.
	 */
	[[nodiscard]] virtual Result<void>
	notify_ui_mode(const UiModeNotification &notification) = 0;
};

} // namespace house_panel::supervisor
