#pragma once
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
namespace media_broker
{
/** A structured, redacted failure: a stable code, a message without audio or paths, context. */
struct MediaError {
	std::string code;
	std::string message;
	std::map<std::string, std::string> context;
};

template <typename Value> class MediaResult final
{
public:
	static MediaResult success(Value value) { return MediaResult(std::move(value)); }
	static MediaResult failure(MediaError error) { return MediaResult(std::move(error)); }
	bool has_value() const { return std::holds_alternative<Value>(storage_); }
	explicit operator bool() const { return has_value(); }
	const Value &value() const
	{
		if (!has_value())
			throw std::logic_error("MEDIA_BROKER.RESULT_VALUE_UNAVAILABLE");
		return std::get<Value>(storage_);
	}
	Value &value()
	{
		if (!has_value())
			throw std::logic_error("MEDIA_BROKER.RESULT_VALUE_UNAVAILABLE");
		return std::get<Value>(storage_);
	}
	const MediaError &error() const
	{
		if (has_value())
			throw std::logic_error("MEDIA_BROKER.RESULT_ERROR_UNAVAILABLE");
		return std::get<MediaError>(storage_);
	}

private:
	explicit MediaResult(Value value) : storage_(std::move(value)) {}
	explicit MediaResult(MediaError error) : storage_(std::move(error)) {}
	std::variant<Value, MediaError> storage_;
};

/** The value of a result that only reports success. */
struct Done {
};

inline MediaError media_error(std::string code, std::string message, std::string operation)
{
	return {std::move(code), std::move(message), {{"operation", std::move(operation)}}};
}
} // namespace media_broker
