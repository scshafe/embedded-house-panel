#pragma once

#include "panel_supervisor/result.hpp"

#include <chrono>

namespace house_panel::supervisor {

// @extension-point supervisor_clock -- use a platform wall clock or deterministic test clock
// implementing this single promise.
class Clock {
public:
	virtual ~Clock() = default;

	/** Returns the current wall-clock instant or a typed acquisition failure. */
	[[nodiscard]] virtual Result<std::chrono::system_clock::time_point> now() = 0;
};

} // namespace house_panel::supervisor
