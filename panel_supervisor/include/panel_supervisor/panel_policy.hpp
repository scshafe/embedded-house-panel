#pragma once

#include "panel_supervisor/panel_state.hpp"
#include "panel_supervisor/policy_decision.hpp"
#include "panel_supervisor/policy_observation.hpp"
#include "panel_supervisor/result.hpp"
#include "panel_supervisor/supervisor_config.hpp"

namespace house_panel::supervisor {

// @canonical panel_state_machine_policy -- the sole authority for presence, solar, and desired
// system transitions.
class PanelPolicy {
public:
	/**
	 * Deterministically evaluates one complete observation against prior state.
	 *
	 * No I/O or wall-clock reads occur here. A clock regression relative to the
	 * remembered presence timestamp returns PANEL_SUPERVISOR.CLOCK_MOVED_BACKWARD;
	 * revision exhaustion returns PANEL_SUPERVISOR.REVISION_OVERFLOW.
	 */
	[[nodiscard]] Result<PolicyDecision> evaluate(const PanelState &previous_state,
						      const PolicyObservation &observation,
						      const SupervisorConfig &config) const;
};

} // namespace house_panel::supervisor
