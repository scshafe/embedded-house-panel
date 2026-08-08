#include "panel_supervisor/rate_limited_stop_aware_suspend.hpp"

#include <chrono>

namespace house_panel::supervisor {

RateLimitedStopAwareSuspend::RateLimitedStopAwareSuspend(
	Suspend &configured_suspend, DaemonStopController &stop_controller,
	const std::chrono::milliseconds minimum_request_interval)
	: configured_suspend_(configured_suspend), stop_controller_(stop_controller),
	  minimum_request_interval_(minimum_request_interval)
{
}

Result<void> RateLimitedStopAwareSuspend::request_suspend()
{
	if (stop_controller_.stop_requested()) {
		return Result<void>::success();
	}
	while (std::chrono::steady_clock::now() < next_request_not_before_) {
		const auto remaining =
			next_request_not_before_ - std::chrono::steady_clock::now();
		auto remaining_milliseconds =
			std::chrono::duration_cast<std::chrono::milliseconds>(remaining);
		if (remaining_milliseconds < remaining) {
			remaining_milliseconds += std::chrono::milliseconds(1);
		}
		const auto wait_result = stop_controller_.wait_for(remaining_milliseconds);
		if (!wait_result) {
			return Result<void>::failure(wait_result.error());
		}
		if (wait_result.value()) {
			return Result<void>::success();
		}
	}
	auto result = configured_suspend_.request_suspend();
	next_request_not_before_ =
		std::chrono::steady_clock::now() + minimum_request_interval_;
	return result;
}

} // namespace house_panel::supervisor
