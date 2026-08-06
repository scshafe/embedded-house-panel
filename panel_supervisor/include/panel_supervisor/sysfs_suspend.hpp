#pragma once

#include "panel_supervisor/suspend.hpp"

#include <string>

namespace house_panel::supervisor {

// @canonical sysfs_suspend -- exact configured Linux power-state write with no fallback mode.
class SysfsSuspend final : public Suspend {
public:
	/**
	 * Validates exact `/sys/power/state` and exact `freeze` or `mem` mode.
	 *
	 * Invalid paths, placeholders, or modes return
	 * PANEL_SUPERVISOR.SUSPEND_CONFIGURATION_INVALID.
	 */
	[[nodiscard]] static Result<SysfsSuspend> create(std::string power_state_path,
							 std::string suspend_mode);

	/**
	 * Writes only the configured mode to the configured state node.
	 *
	 * The call returns when the kernel write returns, normally after resume.
	 * Open/write/close rejection returns PANEL_SUPERVISOR.POWER_STATE_WRITE_FAILED;
	 * another mode is never attempted.
	 */
	[[nodiscard]] Result<void> request_suspend() override;

	[[nodiscard]] const std::string &power_state_path() const noexcept;
	[[nodiscard]] const std::string &suspend_mode() const noexcept;

private:
	SysfsSuspend(std::string power_state_path, std::string suspend_mode);

	std::string power_state_path_;
	std::string suspend_mode_;
};

} // namespace house_panel::supervisor
