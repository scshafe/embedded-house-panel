#pragma once

#include "panel_supervisor/result.hpp"

namespace house_panel::supervisor {

// @extension-point system_suspend -- adapters define the platform handoff while preserving explicit
// success or failure.
class Suspend {
public:
	virtual ~Suspend() = default;

	/**
	 * Requests system suspension and reports whether the platform accepted it.
	 *
	 * Return timing relative to physical sleep is platform-specific. A success
	 * means the request was accepted; wake notification must separately call
	 * PanelSupervisor::resume so policy never assumes an implicit resume.
	 */
	[[nodiscard]] virtual Result<void> request_suspend() = 0;
};

} // namespace house_panel::supervisor
