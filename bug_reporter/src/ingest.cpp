#include "bug_reporter/ingest.h"
#include <nlohmann/json.hpp>
#include <regex>

namespace bug_reporter
{
namespace
{
using Json = nlohmann::json;
}

bool valid_service(const std::string &service)
{
	static const std::regex pattern("[a-z0-9_.-]{1,64}");
	return std::regex_match(service, pattern);
}

IngestLine classify(const std::string &line)
{
	const Json value = Json::parse(line, nullptr, false);
	if (!value.is_object())
		return LogRecord{line, std::nullopt, {}};
	const auto type = value.value("type", std::string());
	if (type == "hello" && value.size() == 2 && value.contains("service") && value["service"].is_string() &&
	    valid_service(value["service"].get<std::string>()))
		return HelloLine{value["service"].get<std::string>()};
	if (type == "exit" && value.size() == 5 && value.contains("service") && value["service"].is_string() &&
	    valid_service(value["service"].get<std::string>()) && value.contains("status") &&
	    value["status"].is_number_integer() && value.contains("signal") && value["signal"].is_number_integer() &&
	    value.contains("restarting") && value["restarting"].is_boolean()) {
		const auto status = value["status"].get<long long>();
		const auto signal = value["signal"].get<long long>();
		if (status >= 0 && status <= 255 && signal >= 0 && signal <= 64)
			return ExitLine{value["service"].get<std::string>(), static_cast<int>(status), static_cast<int>(signal),
					value["restarting"].get<bool>()};
	}
	static const std::regex code("[A-Z][A-Z0-9_]*(\\.[A-Z0-9_]+)+");
	if (value.value("level", std::string()) == "error" && value.value("reportable", std::string()) == "true" &&
	    value.contains("error_code") && value["error_code"].is_string()) {
		const auto error_code = value["error_code"].get<std::string>();
		if (error_code.size() <= 96 && std::regex_match(error_code, code)) {
			auto location = value.value("location", std::string());
			if (location.size() > 256)
				location.resize(256);
			return LogRecord{line, error_code, location};
		}
	}
	return LogRecord{line, std::nullopt, {}};
}

std::string encode_hello(const std::string &service)
{
	return Json({{"type", "hello"}, {"service", service}}).dump();
}

std::string encode_exit(const ExitLine &exit)
{
	return Json({{"type", "exit"},
		     {"service", exit.service},
		     {"status", exit.status},
		     {"signal", exit.signal},
		     {"restarting", exit.restarting}})
		.dump();
}
} // namespace bug_reporter
