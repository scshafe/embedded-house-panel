#include "panel_supervisor/supervisor_error.hpp"

#include <utility>

namespace house_panel::supervisor {

SupervisorError::SupervisorError(const ErrorCode code, std::string message,
				 std::map<std::string, std::string> context, const bool retryable,
				 std::shared_ptr<const SupervisorError> cause)
    : code_(code), message_(std::move(message)), context_(std::move(context)),
      retryable_(retryable), cause_(std::move(cause))
{
}

SupervisorError SupervisorError::wrap(const ErrorCode code, std::string message,
				      const SupervisorError &cause,
				      std::map<std::string, std::string> context,
				      const bool retryable)
{
	return SupervisorError(code, std::move(message), std::move(context), retryable,
			       std::make_shared<const SupervisorError>(cause));
}

ErrorCode SupervisorError::code() const noexcept { return code_; }

const std::string &SupervisorError::message() const noexcept { return message_; }

const std::map<std::string, std::string> &SupervisorError::context() const noexcept
{
	return context_;
}

bool SupervisorError::retryable() const noexcept { return retryable_; }

const std::shared_ptr<const SupervisorError> &SupervisorError::cause() const noexcept
{
	return cause_;
}

} // namespace house_panel::supervisor
