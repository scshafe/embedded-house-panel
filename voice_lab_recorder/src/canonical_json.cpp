#include "voice_lab_recorder/canonical_json.h"
#include <charconv>
#include <cmath>
#include <cstdlib>

namespace voice_lab_recorder
{
namespace
{
bool write(const nlohmann::json &value, std::string &out)
{
	switch (value.type()) {
	case nlohmann::json::value_t::object: {
		// nlohmann's object is a std::map: keys already in byte order.
		out += '{';
		bool first = true;
		for (const auto &[key, member] : value.items()) {
			if (!first)
				out += ',';
			first = false;
			out += nlohmann::json(key).dump(-1, ' ', false, nlohmann::json::error_handler_t::strict);
			out += ':';
			if (!write(member, out))
				return false;
		}
		out += '}';
		return true;
	}
	case nlohmann::json::value_t::array: {
		out += '[';
		for (std::size_t index = 0; index < value.size(); ++index) {
			if (index)
				out += ',';
			if (!write(value[index], out))
				return false;
		}
		out += ']';
		return true;
	}
	case nlohmann::json::value_t::number_float: {
		const double number = value.get<double>();
		if (!std::isfinite(number))
			return false;
		out += python_float_repr(number);
		return true;
	}
	case nlohmann::json::value_t::discarded:
	case nlohmann::json::value_t::binary:
		return false;
	default:
		out += value.dump(-1, ' ', false, nlohmann::json::error_handler_t::strict);
		return true;
	}
}
} // namespace

std::string python_float_repr(const double value)
{
	// Shortest round-trip digits in scientific form: [-]d[.ddd]e[+-]xx.
	char buffer[64];
	const auto converted = std::to_chars(buffer, buffer + sizeof buffer, value, std::chars_format::scientific);
	const std::string text(buffer, converted.ptr);
	const auto e = text.find('e');
	std::string mantissa = text.substr(0, e);
	const int exponent = std::atoi(text.c_str() + e + 1);
	std::string sign;
	if (!mantissa.empty() && mantissa[0] == '-') {
		sign = "-";
		mantissa.erase(0, 1);
	}
	std::string digits;
	for (const char c : mantissa)
		if (c != '.')
			digits += c;
	// Python's repr: exponent notation when the decimal point would sit at or
	// before the fourth zero after it, or past the sixteenth digit.
	const int point = exponent + 1;
	if (point > -4 && point <= 16) {
		std::string fixed;
		if (point <= 0)
			fixed = "0." + std::string(static_cast<std::size_t>(-point), '0') + digits;
		else if (static_cast<std::size_t>(point) >= digits.size())
			fixed = digits + std::string(static_cast<std::size_t>(point) - digits.size(), '0') + ".0";
		else
			fixed = digits.substr(0, static_cast<std::size_t>(point)) + "." +
				digits.substr(static_cast<std::size_t>(point));
		return sign + fixed;
	}
	std::string scientific = digits.substr(0, 1);
	if (digits.size() > 1)
		scientific += "." + digits.substr(1);
	const int magnitude = std::abs(exponent);
	return sign + scientific + (exponent < 0 ? "e-" : "e+") + (magnitude < 10 ? "0" : "") +
	       std::to_string(magnitude);
}

Result<std::string> canonical_json(const nlohmann::json &value)
{
	std::string out;
	try {
		if (write(value, out))
			return Result<std::string>::success(std::move(out));
	} catch (const nlohmann::json::exception &) {
	}
	return Result<std::string>::failure(
		{"VOICE_LAB.CANONICAL_JSON_INVALID", "The record holds a value canonical JSON refuses", {}, false});
}

Result<std::string> canonical_record(const nlohmann::json &value)
{
	auto text = canonical_json(value);
	if (text.has_value())
		text.value() += '\n';
	return text;
}
} // namespace voice_lab_recorder
