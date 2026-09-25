#pragma once

#include "panel_supervisor/suspend.hpp"

#include <optional>
#include <string>

namespace house_panel::supervisor {

// @canonical sysfs_mem_sleep_selection -- exact mem_sleep variant confirmed before every `mem` write.
struct MemSleepSelection {
	/** Exactly `/sys/power/mem_sleep`. */
	std::string mem_sleep_path;
	/** Exactly `s2idle`, `shallow`, or `deep`; never inferred from the kernel default. */
	std::string mem_sleep_state;
};

// @canonical sysfs_suspend -- exact configured Linux power-state write with no fallback mode.
class SysfsSuspend final : public Suspend {
public:
	/**
	 * Validates exact `/sys/power/state` and exact `freeze` or `mem` mode.
	 *
	 * `mem` requires a MemSleepSelection because `mem` means whatever
	 * `/sys/power/mem_sleep` currently selects, and that kernel default differs
	 * by board and firmware. `freeze` rejects a selection. Invalid paths,
	 * placeholders, or modes return PANEL_SUPERVISOR.SUSPEND_CONFIGURATION_INVALID.
	 */
	[[nodiscard]] static Result<SysfsSuspend>
	create(std::string power_state_path, std::string suspend_mode,
	       std::optional<MemSleepSelection> mem_sleep_selection = std::nullopt);

	/**
	 * Writes only the configured mode to the configured state node.
	 *
	 * For `mem`, first reads `/sys/power/mem_sleep`, selects the configured
	 * variant when it is not already selected, and rereads it to confirm the
	 * kernel accepted it. An unlisted variant returns
	 * PANEL_SUPERVISOR.MEM_SLEEP_STATE_UNAVAILABLE; a read, parse, write, or
	 * confirmation failure returns PANEL_SUPERVISOR.MEM_SLEEP_SELECTION_FAILED.
	 * Either way `/sys/power/state` is not written.
	 *
	 * The state write returns when the kernel write returns, normally after
	 * resume. Open/write/close rejection returns
	 * PANEL_SUPERVISOR.POWER_STATE_WRITE_FAILED; another mode or variant is
	 * never attempted.
	 */
	[[nodiscard]] Result<void> request_suspend() override;

	[[nodiscard]] const std::string &power_state_path() const noexcept;
	[[nodiscard]] const std::string &suspend_mode() const noexcept;
	[[nodiscard]] const std::optional<MemSleepSelection> &mem_sleep_selection() const noexcept;

private:
	SysfsSuspend(std::string power_state_path, std::string suspend_mode,
		     std::optional<MemSleepSelection> mem_sleep_selection);

	std::string power_state_path_;
	std::string suspend_mode_;
	std::optional<MemSleepSelection> mem_sleep_selection_;
};

} // namespace house_panel::supervisor
