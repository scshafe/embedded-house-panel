#include "panel_supervisor/system_clock.hpp"

#include "panel_supervisor/error_code.hpp"
#include "system_clock_readiness.hpp"

#include <cerrno>
#include <chrono>
#include <string>

#if defined(__linux__)
#include <sys/timex.h>
#endif

namespace house_panel::supervisor {

namespace detail {

Result<void> validate_system_clock_synchronization_observation(
	const bool query_succeeded, const bool kernel_reports_unsynchronized,
	const int query_error_number)
{
	if (!query_succeeded) {
		return Result<void>::failure(SupervisorError(
			ErrorCode::SystemClockSynchronizationQueryFailed,
			"Failed to query Linux kernel clock synchronization readiness",
			{{"errno", std::to_string(query_error_number)},
			 {"operation", "system_clock.query_synchronization"}},
			query_error_number == EINTR || query_error_number == EAGAIN));
	}
	if (kernel_reports_unsynchronized) {
		return Result<void>::failure(SupervisorError(
			ErrorCode::SystemClockUntrusted,
			"Failed to use the system clock because Linux reports it unsynchronized",
			{{"operation", "system_clock.validate_synchronization"}}, true));
	}
	return Result<void>::success();
}

} // namespace detail

Result<std::chrono::system_clock::time_point> SystemClock::now()
{
#if defined(__linux__)
	timex clock_status{};
	errno = 0;
	const int time_state = ::adjtimex(&clock_status);
	const int query_error_number = errno;
	const bool kernel_reports_unsynchronized =
		time_state == TIME_ERROR ||
		(clock_status.status & (STA_UNSYNC | STA_CLOCKERR)) != 0;
	const auto readiness_result = detail::validate_system_clock_synchronization_observation(
		time_state >= 0, kernel_reports_unsynchronized, query_error_number);
	if (!readiness_result) {
		return Result<std::chrono::system_clock::time_point>::failure(
			readiness_result.error());
	}
	return Result<std::chrono::system_clock::time_point>::success(
		std::chrono::system_clock::now());
#else
	return Result<std::chrono::system_clock::time_point>::failure(SupervisorError(
		ErrorCode::PlatformUnsupported,
		"Failed to prove system clock synchronization because this is not Linux",
		{{"operation", "system_clock.now"}}));
#endif
}

} // namespace house_panel::supervisor
