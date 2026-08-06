#pragma once

#include "panel_supervisor/clock.hpp"

namespace house_panel::supervisor {

// @canonical linux_system_clock -- production wall-clock adapter for supervisor UTC observations.
class SystemClock final : public Clock {
public:
	/**
	 * Returns the Linux system clock only after `adjtimex` reports synchronization.
	 *
	 * Query failure, STA_UNSYNC/STA_CLOCKERR/TIME_ERROR, or a non-Linux build is a
	 * typed failure. An in-range but kernel-untrusted UTC value is never emitted.
	 */
	[[nodiscard]] Result<std::chrono::system_clock::time_point> now() override;
};

} // namespace house_panel::supervisor
