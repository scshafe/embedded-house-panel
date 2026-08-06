#include "panel_supervisor/system_clock.hpp"

#include "panel_supervisor/error_code.hpp"
#include "system_clock_readiness.hpp"

#include <array>
#include <cerrno>
#include <chrono>
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

bool synchronization_observation_rejects_query_failure()
{
	const auto result = detail::validate_system_clock_synchronization_observation(
		false, false, EIO);
	TEST_REQUIRE(!result.has_value());
	TEST_REQUIRE(result.error().code() ==
		     ErrorCode::SystemClockSynchronizationQueryFailed);
	TEST_REQUIRE(result.error().context().at("errno") == std::to_string(EIO));
	TEST_REQUIRE(!result.error().retryable());
	return true;
}

bool synchronization_observation_marks_interrupted_query_retryable()
{
	const auto result = detail::validate_system_clock_synchronization_observation(
		false, false, EINTR);
	TEST_REQUIRE(!result.has_value());
	TEST_REQUIRE(result.error().code() ==
		     ErrorCode::SystemClockSynchronizationQueryFailed);
	TEST_REQUIRE(result.error().retryable());
	return true;
}

bool synchronization_observation_rejects_untrusted_clock()
{
	const auto result =
		detail::validate_system_clock_synchronization_observation(true, true, 0);
	TEST_REQUIRE(!result.has_value());
	TEST_REQUIRE(result.error().code() == ErrorCode::SystemClockUntrusted);
	TEST_REQUIRE(result.error().retryable());
	return true;
}

bool synchronization_observation_accepts_kernel_trust()
{
	const auto result =
		detail::validate_system_clock_synchronization_observation(true, false, 0);
	TEST_REQUIRE(result.has_value());
	return true;
}

bool live_adapter_never_returns_an_unverified_clock()
{
	SystemClock clock;
#if defined(__linux__)
	const auto before = std::chrono::system_clock::now();
	const auto result = clock.now();
	const auto after = std::chrono::system_clock::now();
	if (result.has_value()) {
		TEST_REQUIRE(result.value() >= before);
		TEST_REQUIRE(result.value() <= after);
	} else {
		TEST_REQUIRE(result.error().code() == ErrorCode::SystemClockUntrusted ||
			     result.error().code() ==
				     ErrorCode::SystemClockSynchronizationQueryFailed);
	}
#else
	const auto result = clock.now();
	TEST_REQUIRE(!result.has_value());
	TEST_REQUIRE(result.error().code() == ErrorCode::PlatformUnsupported);
#endif
	return true;
}

using TestFunction = bool (*)();

} // namespace
} // namespace house_panel::supervisor

int main()
{
	using namespace house_panel::supervisor;
	const std::array<std::pair<std::string_view, TestFunction>, 5> tests{{
		{"query failure", synchronization_observation_rejects_query_failure},
		{"interrupted query",
		 synchronization_observation_marks_interrupted_query_retryable},
		{"untrusted clock", synchronization_observation_rejects_untrusted_clock},
		{"trusted clock", synchronization_observation_accepts_kernel_trust},
		{"live adapter", live_adapter_never_returns_an_unverified_clock},
	}};
	for (const auto &[name, test] : tests) {
		if (!test()) {
			std::cerr << "FAILED: " << name << '\n';
			return 1;
		}
	}
	std::cout << "system_clock_test: " << tests.size() << " passed\n";
	return 0;
}
