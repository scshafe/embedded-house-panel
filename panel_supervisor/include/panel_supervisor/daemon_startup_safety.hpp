#pragma once

#include "panel_supervisor/backlight.hpp"
#include "panel_supervisor/error_code.hpp"
#include "panel_supervisor/result.hpp"
#include "panel_supervisor/supervisor_error.hpp"

#include <exception>
#include <string>
#include <utility>

namespace house_panel::supervisor {

inline constexpr int kSafeBacklightOffPercent = 0;

// @canonical daemon_backlight_safety_blank -- fail-closed output before input acquisition or exit.
/**
 * Forces the display backlight to logical zero before daemon input acquisition or exit.
 *
 * Adapter failures and contract-violating exceptions return a typed
 * PANEL_SUPERVISOR.BACKLIGHT_WRITE_FAILED error. No alternate brightness is used.
 */
[[nodiscard]] inline Result<void> force_backlight_off_for_safety(Backlight &backlight)
{
	try {
		auto result = backlight.set_brightness_percent(kSafeBacklightOffPercent);
		if (!result) {
			return Result<void>::failure(SupervisorError::wrap(
				ErrorCode::BacklightWriteFailed,
				"Failed to force the panel backlight off for daemon safety",
				result.error(),
				{{"brightness_percent", "0"},
				 {"operation", "panel_supervisord.force_backlight_off_for_safety"}},
				result.error().retryable()));
		}
	} catch (const std::exception &) {
		return Result<void>::failure(SupervisorError(
			ErrorCode::BacklightWriteFailed,
			"Failed to force the panel backlight off because the adapter threw a standard exception",
			{{"brightness_percent", "0"},
			 {"exception_kind", "standard_exception"},
			 {"operation", "panel_supervisord.force_backlight_off_for_safety"}}));
	} catch (...) {
		return Result<void>::failure(SupervisorError(
			ErrorCode::BacklightWriteFailed,
			"Failed to force the panel backlight off because the adapter threw a non-standard exception",
			{{"brightness_percent", "0"},
			 {"exception_kind", "non_standard_exception"},
			 {"operation", "panel_supervisord.force_backlight_off_for_safety"}}));
	}
	return Result<void>::success();
}

// @canonical daemon_fatal_blank_error -- preserves primary and safety-write evidence.
/**
 * Builds the secondary error emitted when fatal-exit blanking also fails.
 *
 * The returned error retains `blank_error` as its typed cause and records the
 * untouched primary error code, message, and causal-code chain in context.
 */
[[nodiscard]] inline SupervisorError make_fatal_exit_backlight_blank_error(
	const SupervisorError &primary_error, const SupervisorError &blank_error,
	std::string primary_error_chain)
{
	auto context = blank_error.context();
	context.emplace("primary_error_code", std::string(to_string(primary_error.code())));
	context.emplace("primary_error_message", primary_error.message());
	context.emplace("primary_stack_trace", std::move(primary_error_chain));
	return SupervisorError::wrap(
		ErrorCode::BacklightWriteFailed,
		"Failed to force the panel backlight off before terminating for a primary daemon error",
		blank_error, std::move(context), blank_error.retryable());
}

// @canonical daemon_startup_clock_retry -- the only startup failure class the daemon retries.
/**
 * Reports whether a failed initial supervisor cycle is a retryable clock-readiness wait.
 *
 * The top-level error must be the retryable CLOCK_READ_FAILED wrapper produced by
 * PanelSupervisor. Its cause chain must contain a retryable SystemClock readiness
 * failure. All other acquisition, output, logging, and policy failures remain fatal.
 */
[[nodiscard]] inline bool
is_retryable_startup_clock_readiness_failure(const SupervisorError &error) noexcept
{
	if (error.code() != ErrorCode::ClockReadFailed || !error.retryable()) {
		return false;
	}

	auto cause = error.cause();
	while (cause != nullptr) {
		if ((cause->code() == ErrorCode::SystemClockUntrusted ||
		     cause->code() == ErrorCode::SystemClockSynchronizationQueryFailed) &&
		    cause->retryable()) {
			return true;
		}
		cause = cause->cause();
	}
	return false;
}

} // namespace house_panel::supervisor
