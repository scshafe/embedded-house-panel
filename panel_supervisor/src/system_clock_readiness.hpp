#pragma once

#include "panel_supervisor/result.hpp"

namespace house_panel::supervisor::detail {

// @canonical system_clock_readiness -- converts kernel synchronization evidence to trust.
[[nodiscard]] Result<void> validate_system_clock_synchronization_observation(
	bool query_succeeded, bool kernel_reports_unsynchronized, int query_error_number);

} // namespace house_panel::supervisor::detail
