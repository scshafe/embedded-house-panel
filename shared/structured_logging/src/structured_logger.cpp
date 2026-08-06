#include "bhp/structured_logger.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace {

std::string escape_json(const std::string &value)
{
	std::ostringstream escaped;
	for (const unsigned char character : value) {
		switch (character) {
		case '"':
			escaped << "\\\"";
			break;
		case '\\':
			escaped << "\\\\";
			break;
		case '\b':
			escaped << "\\b";
			break;
		case '\f':
			escaped << "\\f";
			break;
		case '\n':
			escaped << "\\n";
			break;
		case '\r':
			escaped << "\\r";
			break;
		case '\t':
			escaped << "\\t";
			break;
		default:
			if (character < 0x20U) {
				escaped << "\\u" << std::hex << std::setw(4)
					<< std::setfill('0') << static_cast<unsigned int>(character);
			} else {
				escaped << character;
			}
		}
	}
	return escaped.str();
}

const char *level_name(const bhp::StructuredLogLevel level)
{
	switch (level) {
	case bhp::StructuredLogLevel::debug:
		return "debug";
	case bhp::StructuredLogLevel::info:
		return "info";
	case bhp::StructuredLogLevel::warn:
		return "warn";
	case bhp::StructuredLogLevel::error:
		return "error";
	}
	throw std::logic_error("STRUCTURED_LOGGING.LEVEL_INVALID");
}

std::string utc_timestamp()
{
	const auto now = std::chrono::system_clock::now();
	const std::time_t now_time = std::chrono::system_clock::to_time_t(now);
	std::tm utc{};
#if defined(_WIN32)
	gmtime_s(&utc, &now_time);
#else
	gmtime_r(&now_time, &utc);
#endif
	std::ostringstream timestamp;
	timestamp << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
	return timestamp.str();
}

} // namespace

namespace bhp {

StructuredLogger::StructuredLogger(std::string service_name, std::ostream &output_stream)
	: service_name_(std::move(service_name)), output_stream_(output_stream)
{
	if (service_name_.empty()) {
		throw std::invalid_argument("STRUCTURED_LOGGING.SERVICE_NAME_EMPTY");
	}
}

void StructuredLogger::log(const StructuredLogLevel level,
			   const std::string &message,
			   const std::string &trace_id,
			   const std::map<std::string, std::string> &context_fields)
{
	std::ostringstream record;
	record << "{\"timestamp\":\"" << utc_timestamp() << "\",\"level\":\""
	       << level_name(level) << "\",\"message\":\"" << escape_json(message)
	       << "\",\"service\":\"" << escape_json(service_name_)
	       << "\",\"trace_id\":\"" << escape_json(trace_id) << '"';
	for (const auto &[key, value] : context_fields) {
		record << ",\"" << escape_json(key) << "\":\"" << escape_json(value) << '"';
	}
	record << "}\n";
	output_stream_ << record.str();
	output_stream_.flush();
	if (!output_stream_) {
		throw std::runtime_error("STRUCTURED_LOGGING.OUTPUT_STREAM_WRITE_FAILED");
	}
}

void StructuredLogger::log_error(
	const std::string &message,
	const std::string &trace_id,
	const std::string &error_code,
	const std::string &error_message,
	const std::string &stack_trace,
	const std::map<std::string, std::string> &context_fields)
{
	auto error_fields = context_fields;
	error_fields.emplace("error_code", error_code);
	error_fields.emplace("error_message", error_message);
	error_fields.emplace("stack_trace", stack_trace);
	log(StructuredLogLevel::error, message, trace_id, error_fields);
}

} // namespace bhp
