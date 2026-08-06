#pragma once

#include "panel_supervisor/backlight.hpp"
#include "panel_supervisor/clock.hpp"
#include "panel_supervisor/panel_policy.hpp"
#include "panel_supervisor/panel_state.hpp"
#include "panel_supervisor/presence_source.hpp"
#include "panel_supervisor/result.hpp"
#include "panel_supervisor/solar_calculator.hpp"
#include "panel_supervisor/supervisor_config.hpp"
#include "panel_supervisor/supervisor_event.hpp"
#include "panel_supervisor/suspend.hpp"
#include "panel_supervisor/ui_mode_notifier.hpp"

#include "bhp/structured_logger.h"

#include <map>
#include <string>

namespace house_panel::supervisor {

// @canonical panel_supervisor -- sole side-effecting orchestrator for panel lifecycle policy.
class PanelSupervisor {
public:
	/**
	 * Wires validated policy configuration to all platform capabilities.
	 *
	 * Every referenced dependency must outlive this object. The constructor does
	 * no I/O; start, poll, and resume return all operational failures explicitly.
	 */
	PanelSupervisor(SupervisorConfig config, PanelPolicy policy,
			PresenceSource &presence_source, Clock &clock,
			SolarCalculator &solar_calculator, Backlight &backlight,
			UiModeNotifier &ui_mode_notifier, Suspend &suspend,
			bhp::StructuredLogger &logger);

	PanelSupervisor(const PanelSupervisor &) = delete;
	PanelSupervisor &operator=(const PanelSupervisor &) = delete;
	PanelSupervisor(PanelSupervisor &&) = delete;
	PanelSupervisor &operator=(PanelSupervisor &&) = delete;

	/** Acquires fresh inputs and applies the initial deterministic state. */
	[[nodiscard]] Result<PanelState> start(std::string trace_id);

	/** Acquires fresh inputs and reconciles one normal polling cycle. */
	[[nodiscard]] Result<PanelState> poll(std::string trace_id);

	/**
	 * Marks resume recomputation, then reacquires clock, presence, and solar state.
	 *
	 * No pre-suspend solar or brightness output is restored. Any acquisition,
	 * output, logging, or suspend failure is returned as a typed SupervisorError.
	 */
	[[nodiscard]] Result<PanelState> resume(std::string trace_id);

	/** Returns the latest inspectable state, including fault/intermediate state. */
	[[nodiscard]] const PanelState &state() const noexcept;

private:
	[[nodiscard]] Result<PanelState> recompute(SupervisorEvent event,
						   const std::string &trace_id);
	[[nodiscard]] Result<void> log_external_call(const std::string &trace_id,
						     std::string target, std::string operation,
						     long long duration_ms, bool succeeded,
						     const SupervisorError *error = nullptr);
	[[nodiscard]] Result<PanelState>
	fail_cycle(const std::string &trace_id, SupervisorEvent event, SupervisorError error,
		   std::map<std::string, std::string> context = {});
	[[nodiscard]] Result<void> validate_trace_id(const std::string &trace_id) const;

	SupervisorConfig config_;
	PanelPolicy policy_;
	PresenceSource &presence_source_;
	Clock &clock_;
	SolarCalculator &solar_calculator_;
	Backlight &backlight_;
	UiModeNotifier &ui_mode_notifier_;
	Suspend &suspend_;
	bhp::StructuredLogger &logger_;
	PanelState state_;
};

} // namespace house_panel::supervisor
