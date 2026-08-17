#pragma once

#include <chrono>

namespace house_panel::panel_ui {

// @extension-point panel_ui_monotonic_clock -- inject production steady time or deterministic time.
class MonotonicClock {
public:
	virtual ~MonotonicClock() = default;
	[[nodiscard]] virtual std::chrono::steady_clock::time_point now() const noexcept = 0;
};

} // namespace house_panel::panel_ui
