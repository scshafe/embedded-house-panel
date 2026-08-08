#include "panel_supervisor/rate_limited_stop_aware_suspend.hpp"

#include <array>
#include <chrono>
#include <csignal>
#include <iostream>
#include <string_view>
#include <utility>
#include <vector>

namespace house_panel::supervisor {
namespace {

#define TEST_REQUIRE(condition)                                                                    \
	do {                                                                                       \
		if (!(condition)) {                                                                \
			std::cerr << __func__ << ": requirement failed: " #condition << '\n';      \
			return false;                                                              \
		}                                                                                  \
	} while (false)

class RecordingSuspend final : public Suspend {
public:
	Result<void> request_suspend() override
	{
		request_times_.push_back(std::chrono::steady_clock::now());
		return Result<void>::success();
	}

	[[nodiscard]] const std::vector<std::chrono::steady_clock::time_point> &
	request_times() const noexcept
	{
		return request_times_;
	}

private:
	std::vector<std::chrono::steady_clock::time_point> request_times_;
};

bool second_request_waits_for_configured_minimum_interval()
{
	auto controller_result = DaemonStopController::create();
	TEST_REQUIRE(controller_result.has_value());
	auto controller = std::move(controller_result).value();
	RecordingSuspend configured_suspend;
	const auto minimum_request_interval = std::chrono::milliseconds(40);
	RateLimitedStopAwareSuspend suspend(configured_suspend, *controller,
					    minimum_request_interval);

	TEST_REQUIRE(suspend.request_suspend().has_value());
	TEST_REQUIRE(suspend.request_suspend().has_value());
	TEST_REQUIRE(configured_suspend.request_times().size() == 2);
	const auto observed_request_interval = configured_suspend.request_times()[1] -
					       configured_suspend.request_times()[0];
	TEST_REQUIRE(observed_request_interval >= minimum_request_interval);
	return true;
}

bool stop_request_suppresses_configured_suspend_call()
{
	auto controller_result = DaemonStopController::create();
	TEST_REQUIRE(controller_result.has_value());
	auto controller = std::move(controller_result).value();
	RecordingSuspend configured_suspend;
	RateLimitedStopAwareSuspend suspend(configured_suspend, *controller,
					    std::chrono::milliseconds(40));

	TEST_REQUIRE(::raise(SIGTERM) == 0);
	TEST_REQUIRE(controller->stop_requested());
	TEST_REQUIRE(suspend.request_suspend().has_value());
	TEST_REQUIRE(configured_suspend.request_times().empty());
	return true;
}

using TestFunction = bool (*)();

} // namespace
} // namespace house_panel::supervisor

int main()
{
	using namespace house_panel::supervisor;
	const std::array<std::pair<std::string_view, TestFunction>, 2> tests{{
		{"configured minimum request interval",
		 second_request_waits_for_configured_minimum_interval},
		{"stop suppresses suspend", stop_request_suppresses_configured_suspend_call},
	}};
	for (const auto &[name, test] : tests) {
		if (!test()) {
			std::cerr << "FAILED: " << name << '\n';
			return 1;
		}
	}
	std::cout << "rate_limited_stop_aware_suspend_test: " << tests.size()
		  << " passed\n";
	return 0;
}
