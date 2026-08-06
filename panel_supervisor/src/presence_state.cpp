#include "panel_supervisor/presence_state.hpp"

namespace house_panel::supervisor {

std::string_view to_string(const PresenceState presence_state) noexcept
{
	switch (presence_state) {
	case PresenceState::Unknown:
		return "unknown";
	case PresenceState::Present:
		return "present";
	case PresenceState::GracePeriod:
		return "grace_period";
	case PresenceState::Absent:
		return "absent";
	}
	return "unrecognized_presence_state";
}

} // namespace house_panel::supervisor
