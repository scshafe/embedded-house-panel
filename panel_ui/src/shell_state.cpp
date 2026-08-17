#include "panel_ui/shell_state.hpp"

namespace house_panel::panel_ui {

std::string_view to_string(const ShellTransitionState transition_state) noexcept
{
	switch (transition_state) {
	case ShellTransitionState::Stable:
		return "stable";
	case ShellTransitionState::Loading:
		return "loading";
	case ShellTransitionState::Committing:
		return "committing";
	case ShellTransitionState::Suspending:
		return "suspending";
	case ShellTransitionState::Resuming:
		return "resuming";
	}
	return "unrecognized_transition";
}

} // namespace house_panel::panel_ui
