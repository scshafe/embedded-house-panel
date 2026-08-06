#pragma once

#include "panel_supervisor/result.hpp"

#include <chrono>
#include <memory>
#include <signal.h>

namespace house_panel::supervisor {

// @canonical daemon_stop_controller -- process-wide signal wakeup boundary for panel_supervisord.
class DaemonStopController final {
public:
	/**
	 * Installs SIGINT/SIGTERM handlers backed by one nonblocking self-pipe.
	 *
	 * Only one controller may be active in a process. Pipe, descriptor, or signal
	 * setup failure returns a typed error without leaving a partial controller.
	 */
	[[nodiscard]] static Result<std::unique_ptr<DaemonStopController>> create();

	~DaemonStopController();

	DaemonStopController(const DaemonStopController &) = delete;
	DaemonStopController &operator=(const DaemonStopController &) = delete;
	DaemonStopController(DaemonStopController &&) = delete;
	DaemonStopController &operator=(DaemonStopController &&) = delete;

	/**
	 * Waits for the complete non-negative duration or a process stop signal.
	 *
	 * Returns true when SIGINT/SIGTERM requested shutdown and false on timeout.
	 * Invalid duration or poll/read failure returns PANEL_SUPERVISOR.DAEMON_STOP_WAIT_FAILED.
	 */
	[[nodiscard]] Result<bool> wait_for(std::chrono::milliseconds duration);

	[[nodiscard]] bool stop_requested() const noexcept;

private:
	DaemonStopController(int read_descriptor, int write_descriptor,
			     const struct sigaction &previous_sigint,
			     const struct sigaction &previous_sigterm) noexcept;

	int read_descriptor_;
	int write_descriptor_;
	struct sigaction previous_sigint_;
	struct sigaction previous_sigterm_;
};

} // namespace house_panel::supervisor
