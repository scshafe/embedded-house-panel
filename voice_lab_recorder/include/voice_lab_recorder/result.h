#pragma once
#include <map>
#include <string>
#include <utility>
#include <variant>

namespace voice_lab_recorder
{
/** A structured, content-free failure: a stable code, a short message and safe context. */
struct Error {
	std::string code;
	std::string message;
	std::map<std::string, std::string> context;
	bool retryable{false};
};

template <typename Value> class Result final
{
public:
	static Result success(Value value) { return Result(std::move(value)); }
	static Result failure(Error error) { return Result(std::move(error)); }
	bool has_value() const { return std::holds_alternative<Value>(storage_); }
	explicit operator bool() const { return has_value(); }
	const Value &value() const { return std::get<Value>(storage_); }
	Value &value() { return std::get<Value>(storage_); }
	const Error &error() const { return std::get<Error>(storage_); }

private:
	explicit Result(Value value) : storage_(std::move(value)) {}
	explicit Result(Error error) : storage_(std::move(error)) {}
	std::variant<Value, Error> storage_;
};
} // namespace voice_lab_recorder
