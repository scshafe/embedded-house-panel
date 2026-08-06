#pragma once

#include "panel_supervisor/result.hpp"

namespace house_panel::supervisor {

// @extension-point presence_source -- implement this contract for any presence technology; true
// means presence is detected now.
class PresenceSource {
public:
	virtual ~PresenceSource() = default;

	/**
	 * Reads current raw presence without applying grace-period policy.
	 *
	 * Hardware failures must return a typed SupervisorError and must never be
	 * converted to `false`, because absence and failed acquisition are distinct.
	 */
	[[nodiscard]] virtual Result<bool> presence_detected() = 0;
};

} // namespace house_panel::supervisor
