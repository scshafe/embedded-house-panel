#include "panel_supervisor/touch_runtime_configuration.hpp"
#include <charconv>
#include <limits>

namespace house_panel::supervisor {
Result<std::optional<TouchRuntimeConfiguration>> TouchRuntimeConfiguration::parse(
	const std::map<std::string, std::string> &fields) {
	using Output = Result<std::optional<TouchRuntimeConfiguration>>;
	std::array<std::string, 13> values; bool any = false;
	std::map<std::string, std::string> errors;
	for (std::size_t i = 0; i < values.size(); ++i) {
		auto it = fields.find(kTouchRuntimeEnvironmentFields[i]);
		if (it != fields.end()) { any = true; values[i] = it->second; }
	}
	if (!any) return Output::success(std::nullopt);
	for (std::size_t i = 0; i < values.size(); ++i)
		if (values[i].empty() || values[i].find("REPLACE_ME") != std::string::npos
			|| values[i].find('\n') != std::string::npos || values[i].find('\0') != std::string::npos)
			errors[kTouchRuntimeEnvironmentFields[i]] = "required_explicit_touch_value";
	auto number = [&](std::size_t i, long long low, long long high) {
		long long value{}; auto parsed = std::from_chars(values[i].data(), values[i].data() + values[i].size(), value);
		if (parsed.ec != std::errc{} || parsed.ptr != values[i].data() + values[i].size() || value < low || value > high)
			errors[kTouchRuntimeEnvironmentFields[i]] = "touch_numeric_bound_invalid";
		return value;
	};
	TouchRuntimeConfiguration configuration{};
	configuration.device.path = values[0]; configuration.device.exact_name = values[1];
	if (values[2] == "single") configuration.device.protocol = TouchEvdevProtocol::SingleContact;
	else if (values[2] == "type_b") configuration.device.protocol = TouchEvdevProtocol::TypeB;
	else errors[kTouchRuntimeEnvironmentFields[2]] = "must_be_single_or_type_b";
	configuration.device.x_minimum = static_cast<std::int32_t>(number(3, INT32_MIN, INT32_MAX));
	configuration.device.x_maximum = static_cast<std::int32_t>(number(4, INT32_MIN, INT32_MAX));
	configuration.device.y_minimum = static_cast<std::int32_t>(number(5, INT32_MIN, INT32_MAX));
	configuration.device.y_maximum = static_cast<std::int32_t>(number(6, INT32_MIN, INT32_MAX));
	configuration.device.contact_key = static_cast<std::uint16_t>(number(7, 0, 330));
	configuration.device.slot_count = static_cast<std::uint32_t>(number(8, 1, 32));
	configuration.socket_path = values[9];
	if (values[9].empty() || values[9][0] != '/' || values[9].size() >= 100)
		errors[kTouchRuntimeEnvironmentFields[9]] = "must_be_absolute_bounded_socket_path";
	configuration.authority.observation_ttl = std::chrono::milliseconds(number(10, 1, 5000));
	configuration.authority.maximum_contact_duration = std::chrono::milliseconds(number(11, 1, 60000));
	configuration.authority.maximum_retained_observations = static_cast<std::size_t>(number(12, 1, 1024));
	if (!EvdevTouchParser::create(configuration.device)) errors["touch_device_configuration"] = "invalid_protocol_axes_or_slot_configuration";
	if (!errors.empty()) return Output::failure({ErrorCode::DaemonConfigurationInvalid,
		"Explicit touch configuration is incomplete or invalid", std::move(errors)});
	return Output::success(std::move(configuration));
}
}
