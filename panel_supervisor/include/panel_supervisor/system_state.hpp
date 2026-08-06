#pragma once

#include <string_view>

namespace house_panel::supervisor {

// @canonical panel_system_state -- explicit lifecycle state owned by PanelSupervisor.
enum class SystemState {
	Starting,
	Active,
	Suspended,
	RecomputingAfterResume,
	Faulted,
};

/** Returns the stable snake_case representation used in logs and diagnostics. */
[[nodiscard]] std::string_view to_string(SystemState system_state) noexcept;

} // namespace house_panel::supervisor
