#include "panel_supervisor/suspend_mode.hpp"

namespace house_panel::supervisor {

std::string_view to_string(const SuspendMode suspend_mode) noexcept
{
	switch (suspend_mode) {
	case SuspendMode::Disabled:
		return "disabled";
	case SuspendMode::Freeze:
		return "freeze";
	case SuspendMode::Mem:
		return "mem";
	}
	return "unrecognized_suspend_mode";
}

} // namespace house_panel::supervisor
