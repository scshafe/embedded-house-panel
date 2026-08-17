#pragma once

#include "bhp/touch_observation_protocol/error.h"

#include <utility>
#include <variant>

namespace bhp::touch_observation_protocol {

template <typename T> class Result {
public:
	[[nodiscard]] static Result success(T value)
	{
		return Result(std::in_place_index<0>, std::move(value));
	}

	[[nodiscard]] static Result failure(Error error)
	{
		return Result(std::in_place_index<1>, std::move(error));
	}

	[[nodiscard]] bool has_value() const noexcept { return value_.index() == 0; }
	explicit operator bool() const noexcept { return has_value(); }

	[[nodiscard]] const T &value() const & { return std::get<0>(value_); }
	[[nodiscard]] T &value() & { return std::get<0>(value_); }
	[[nodiscard]] T &&value() && { return std::get<0>(std::move(value_)); }
	[[nodiscard]] const Error &error() const & { return std::get<1>(value_); }

private:
	template <std::size_t Index, typename Value>
	explicit Result(std::in_place_index_t<Index> index, Value &&value)
		: value_(index, std::forward<Value>(value))
	{
	}

	std::variant<T, Error> value_;
};

template <> class Result<void> {
public:
	[[nodiscard]] static Result success()
	{
		return Result(std::in_place_index<0>, std::monostate{});
	}

	[[nodiscard]] static Result failure(Error error)
	{
		return Result(std::in_place_index<1>, std::move(error));
	}

	[[nodiscard]] bool has_value() const noexcept { return value_.index() == 0; }
	explicit operator bool() const noexcept { return has_value(); }
	[[nodiscard]] const Error &error() const & { return std::get<1>(value_); }

private:
	template <std::size_t Index, typename Value>
	explicit Result(std::in_place_index_t<Index> index, Value &&value)
		: value_(index, std::forward<Value>(value))
	{
	}

	std::variant<std::monostate, Error> value_;
};

} // namespace bhp::touch_observation_protocol
