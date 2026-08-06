#include "panel_supervisor/supervisor_event.hpp"

namespace house_panel::supervisor {

std::string_view to_string(const SupervisorEvent supervisor_event) noexcept
{
	switch (supervisor_event) {
	case SupervisorEvent::Start:
		return "start";
	case SupervisorEvent::Poll:
		return "poll";
	case SupervisorEvent::Resume:
		return "resume";
	}
	return "unrecognized_supervisor_event";
}

} // namespace house_panel::supervisor
