#pragma once

#include "panel_supervisor/daemon_stop_controller.hpp"
#include "panel_supervisor/suspend.hpp"

#include <chrono>

namespace house_panel::supervisor {

// @canonical rate_limited_stop_aware_suspend -- enforces the configured interval between
// panel_supervisord suspend calls while making both the wait and final call stop-aware.
class RateLimitedStopAwareSuspend final : public Suspend {
public:
	RateLimitedStopAwareSuspend(Suspend &configured_suspend,
				    DaemonStopController &stop_controller,
				    std::chrono::milliseconds minimum_request_interval);

	/**
	 * Waits until the next request is allowed, unless daemon shutdown was requested.
	 *
	 * Stop requests return success without calling the configured suspend adapter. Wait
	 * failures and configured-adapter results are returned without substitution.
	 */
	[[nodiscard]] Result<void> request_suspend() override;

private:
	Suspend &configured_suspend_;
	DaemonStopController &stop_controller_;
	std::chrono::milliseconds minimum_request_interval_;
	std::chrono::steady_clock::time_point next_request_not_before_{
		std::chrono::steady_clock::time_point::min()};
};

} // namespace house_panel::supervisor
