#include "panel_supervisor/daemon_stop_controller.hpp"

#include "panel_supervisor/error_code.hpp"

#include <array>
#include <chrono>
#include <csignal>
#include <iostream>
#include <string_view>
#include <utility>

namespace house_panel::supervisor {
namespace {

#define TEST_REQUIRE(condition)                                                                    \
	do {                                                                                       \
		if (!(condition)) {                                                                \
			std::cerr << __func__ << ": requirement failed: " #condition << '\n';      \
			return false;                                                              \
		}                                                                                  \
	} while (false)

bool timeout_and_invalid_duration_are_explicit()
{
	auto controller_result = DaemonStopController::create();
	TEST_REQUIRE(controller_result.has_value());
	auto controller = std::move(controller_result).value();
	const auto timeout_result = controller->wait_for(std::chrono::milliseconds(2));
	TEST_REQUIRE(timeout_result.has_value());
	TEST_REQUIRE(!timeout_result.value());
	const auto invalid_result = controller->wait_for(std::chrono::milliseconds(-1));
	TEST_REQUIRE(!invalid_result.has_value());
	TEST_REQUIRE(invalid_result.error().code() == ErrorCode::DaemonStopWaitFailed);
	return true;
}

bool signal_before_wait_is_retained_and_interrupts_immediately()
{
	auto controller_result = DaemonStopController::create();
	TEST_REQUIRE(controller_result.has_value());
	auto controller = std::move(controller_result).value();
	TEST_REQUIRE(::raise(SIGTERM) == 0);
	const auto started_at = std::chrono::steady_clock::now();
	const auto wait_result = controller->wait_for(std::chrono::seconds(2));
	const auto elapsed = std::chrono::steady_clock::now() - started_at;
	TEST_REQUIRE(wait_result.has_value());
	TEST_REQUIRE(wait_result.value());
	TEST_REQUIRE(controller->stop_requested());
	TEST_REQUIRE(elapsed < std::chrono::milliseconds(100));
	return true;
}

bool second_process_wide_controller_is_rejected()
{
	auto first_result = DaemonStopController::create();
	TEST_REQUIRE(first_result.has_value());
	auto first = std::move(first_result).value();
	const auto second_result = DaemonStopController::create();
	TEST_REQUIRE(!second_result.has_value());
	TEST_REQUIRE(second_result.error().code() ==
		     ErrorCode::DaemonStopControllerAlreadyActive);
	return true;
}

using TestFunction = bool (*)();

} // namespace
} // namespace house_panel::supervisor

int main()
{
	using namespace house_panel::supervisor;
	const std::array<std::pair<std::string_view, TestFunction>, 3> tests{{
		{"timeout and invalid duration", timeout_and_invalid_duration_are_explicit},
		{"retained signal", signal_before_wait_is_retained_and_interrupts_immediately},
		{"singleton controller", second_process_wide_controller_is_rejected},
	}};
	for (const auto &[name, test] : tests) {
		if (!test()) {
			std::cerr << "FAILED: " << name << '\n';
			return 1;
		}
	}
	std::cout << "daemon_stop_controller_test: " << tests.size() << " passed\n";
	return 0;
}
