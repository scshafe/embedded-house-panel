#pragma once

#include "panel_supervisor/error_code.hpp"

#include <map>
#include <memory>
#include <string>
#include <utility>

namespace house_panel::supervisor {

// @canonical panel_supervisor_error -- all domain and adapter failures cross boundaries in this
// shape.
class SupervisorError {
public:
	/**
	 * Builds a typed supervisor failure.
	 *
	 * `message` must describe the failed operation. `context` must contain only
	 * non-secret diagnostic values. `cause` preserves a wrapped lower-level
	 * failure. Construction does not throw except for allocation failure.
	 */
	SupervisorError(ErrorCode code, std::string message,
			std::map<std::string, std::string> context = {}, bool retryable = false,
			std::shared_ptr<const SupervisorError> cause = nullptr);

	/** Wraps `cause` while preserving its complete typed failure chain. */
	[[nodiscard]] static SupervisorError wrap(ErrorCode code, std::string message,
						  const SupervisorError &cause,
						  std::map<std::string, std::string> context = {},
						  bool retryable = false);

	[[nodiscard]] ErrorCode code() const noexcept;
	[[nodiscard]] const std::string &message() const noexcept;
	[[nodiscard]] const std::map<std::string, std::string> &context() const noexcept;
	[[nodiscard]] bool retryable() const noexcept;
	[[nodiscard]] const std::shared_ptr<const SupervisorError> &cause() const noexcept;

private:
	ErrorCode code_;
	std::string message_;
	std::map<std::string, std::string> context_;
	bool retryable_;
	std::shared_ptr<const SupervisorError> cause_;
};

} // namespace house_panel::supervisor
