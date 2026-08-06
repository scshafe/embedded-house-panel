#pragma once

#include "panel_supervisor/supervisor_error.hpp"

#include <utility>
#include <variant>

namespace house_panel::supervisor {

// @canonical panel_supervisor_result -- explicit success/failure return used across every module
// boundary.
template <typename T> class [[nodiscard]] Result {
public:
	/** Constructs a successful result containing `value`. */
	[[nodiscard]] static Result success(T value) { return Result(std::move(value)); }

	/** Constructs a failed result containing the complete typed `error`. */
	[[nodiscard]] static Result failure(SupervisorError error)
	{
		return Result(std::move(error));
	}

	[[nodiscard]] bool has_value() const noexcept
	{
		return std::holds_alternative<T>(storage_);
	}

	explicit operator bool() const noexcept { return has_value(); }

	/** Returns the value. Calling this on a failure violates the API precondition. */
	[[nodiscard]] const T &value() const & { return std::get<T>(storage_); }
	[[nodiscard]] T &value() & { return std::get<T>(storage_); }
	[[nodiscard]] T &&value() && { return std::get<T>(std::move(storage_)); }

	/** Returns the error. Calling this on a success violates the API precondition. */
	[[nodiscard]] const SupervisorError &error() const &
	{
		return std::get<SupervisorError>(storage_);
	}

	[[nodiscard]] SupervisorError &&error() &&
	{
		return std::get<SupervisorError>(std::move(storage_));
	}

private:
	explicit Result(T value) : storage_(std::move(value)) {}
	explicit Result(SupervisorError error) : storage_(std::move(error)) {}

	std::variant<T, SupervisorError> storage_;
};

template <> class [[nodiscard]] Result<void> {
public:
	/** Constructs a successful result with no payload. */
	[[nodiscard]] static Result success() { return Result(std::monostate{}); }

	/** Constructs a failed result containing the complete typed `error`. */
	[[nodiscard]] static Result failure(SupervisorError error)
	{
		return Result(std::move(error));
	}

	[[nodiscard]] bool has_value() const noexcept
	{
		return std::holds_alternative<std::monostate>(storage_);
	}

	explicit operator bool() const noexcept { return has_value(); }

	/** Returns the error. Calling this on a success violates the API precondition. */
	[[nodiscard]] const SupervisorError &error() const &
	{
		return std::get<SupervisorError>(storage_);
	}

	[[nodiscard]] SupervisorError &&error() &&
	{
		return std::get<SupervisorError>(std::move(storage_));
	}

private:
	explicit Result(std::monostate value) : storage_(value) {}
	explicit Result(SupervisorError error) : storage_(std::move(error)) {}

	std::variant<std::monostate, SupervisorError> storage_;
};

} // namespace house_panel::supervisor
