#pragma once

#include <string_view>

namespace house_panel::supervisor {

// @canonical panel_supervisor_event -- externally visible causes of deterministic recomputation.
enum class SupervisorEvent {
	Start,
	Poll,
	Resume,
};

/** Returns the stable snake_case representation used in logs and diagnostics. */
[[nodiscard]] std::string_view to_string(SupervisorEvent supervisor_event) noexcept;

} // namespace house_panel::supervisor
