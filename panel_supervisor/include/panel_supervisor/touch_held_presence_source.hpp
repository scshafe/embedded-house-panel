#pragma once

#include "panel_supervisor/presence_source.hpp"

#include <chrono>
#include <functional>
#include <optional>
#include <utility>

namespace house_panel::supervisor {

/**
 * Presence is the inner source's, or a touch contact that started less than `hold` ago.
 *
 * Off unless BHP_PANEL_SUPERVISOR_TOUCH_PRESENCE_SECONDS is set. The bench tier (ADR 0014)
 * sets it because it has no PIR; production leaves it unset until the PIR's policy is
 * measured. An inner failure stays a failure, never absence, whatever the touch says.
 */
class TouchHeldPresenceSource final : public PresenceSource {
public:
	using Clock = std::chrono::steady_clock;

	TouchHeldPresenceSource(PresenceSource &inner,
				std::function<std::optional<Clock::time_point>()> last_contact_started,
				std::chrono::seconds hold,
				std::function<Clock::time_point()> now = [] { return Clock::now(); })
	    : inner_(inner), last_contact_started_(std::move(last_contact_started)), hold_(hold),
	      now_(std::move(now))
	{
	}

	[[nodiscard]] Result<bool> presence_detected() override
	{
		auto inner = inner_.presence_detected();
		if (!inner || inner.value()) return inner;
		const auto started = last_contact_started_();
		return Result<bool>::success(started.has_value() && now_() - *started < hold_);
	}

private:
	PresenceSource &inner_;
	std::function<std::optional<Clock::time_point>()> last_contact_started_;
	std::chrono::seconds hold_;
	std::function<Clock::time_point()> now_;
};

} // namespace house_panel::supervisor
