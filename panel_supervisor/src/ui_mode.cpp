#include "panel_supervisor/ui_mode.hpp"

namespace house_panel::supervisor {

std::string_view to_string(const UiMode ui_mode) noexcept
{
	switch (ui_mode) {
	case UiMode::Day:
		return "day";
	case UiMode::Night:
		return "night";
	case UiMode::Suspended:
		return "suspended";
	}
	return "unrecognized_ui_mode";
}

} // namespace house_panel::supervisor
