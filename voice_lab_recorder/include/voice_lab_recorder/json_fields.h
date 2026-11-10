#pragma once
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>

/**
 * Field reads for JSON that another party wrote: voice-lab's answers, the
 * broker's capture record, the outbox's own record. A missing field, a field
 * of another type, or a value that is not an object reads as the fallback.
 * nlohmann's `value(key, default)` throws on the last two, and an exception
 * there would end the uploader.
 */
namespace voice_lab_recorder
{
inline nlohmann::json json_field(const nlohmann::json &object, const char *key)
{
	if (!object.is_object())
		return nullptr;
	const auto found = object.find(key);
	return found == object.end() ? nlohmann::json(nullptr) : *found;
}

inline std::string text_field(const nlohmann::json &object, const char *key, std::string fallback = {})
{
	const auto value = json_field(object, key);
	return value.is_string() ? value.get<std::string>() : fallback;
}

inline std::uint64_t count_field(const nlohmann::json &object, const char *key, std::uint64_t fallback = 0)
{
	const auto value = json_field(object, key);
	return value.is_number_unsigned() ? value.get<std::uint64_t>() : fallback;
}

inline bool flag_field(const nlohmann::json &object, const char *key, bool fallback = false)
{
	const auto value = json_field(object, key);
	return value.is_boolean() ? value.get<bool>() : fallback;
}

inline nlohmann::json array_field(const nlohmann::json &object, const char *key)
{
	const auto value = json_field(object, key);
	return value.is_array() ? value : nlohmann::json::array();
}

inline nlohmann::json object_field(const nlohmann::json &object, const char *key)
{
	const auto value = json_field(object, key);
	return value.is_object() ? value : nlohmann::json::object();
}
} // namespace voice_lab_recorder
