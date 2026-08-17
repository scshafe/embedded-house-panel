#pragma once

#include "bhp/panel_mode_protocol/types.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace house_panel::panel_ui {

using LifecycleMode = bhp::panel_mode_protocol::DisplayMode;
enum class ShellTransitionState { Stable, Loading, Committing, Suspending, Resuming };

[[nodiscard]] std::string_view to_string(ShellTransitionState transition_state) noexcept;

struct ShellStateSnapshot {
	std::string active_context_nonce;
	std::string active_surface_id;
	LifecycleMode lifecycle_mode;
	ShellTransitionState transition_state;
	std::uint64_t render_generation;
};

} // namespace house_panel::panel_ui
