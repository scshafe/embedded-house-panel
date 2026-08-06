#include "bhp/structured_log_level.h"
#include "bhp/structured_logger.h"

#include <array>
#include <iostream>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <string_view>
#include <utility>

namespace {

#define TEST_REQUIRE(condition)                                                                    \
	do {                                                                                       \
		if (!(condition)) {                                                                \
			std::cerr << __func__ << ": requirement failed: " #condition << '\n';      \
			return false;                                                              \
		}                                                                                  \
	} while (false)

class RejectingStreamBuffer final : public std::streambuf {
protected:
	std::streamsize xsputn(const char *, std::streamsize) override { return 0; }
	int_type overflow(int_type) override { return traits_type::eof(); }
};

class FlushFailingStreamBuffer final : public std::stringbuf {
protected:
	int sync() override { return -1; }
};

bool successful_record_is_complete_json_line()
{
	std::ostringstream output;
	bhp::StructuredLogger logger("structured_logger_test", output);
	logger.log(bhp::StructuredLogLevel::info, "quoted \"message\"", "trace-1",
		   {{"result", "ok"}});
	const std::string record = output.str();
	TEST_REQUIRE(!record.empty());
	TEST_REQUIRE(record.back() == '\n');
	TEST_REQUIRE(record.find("\"level\":\"info\"") != std::string::npos);
	TEST_REQUIRE(record.find("\"service\":\"structured_logger_test\"") !=
		     std::string::npos);
	TEST_REQUIRE(record.find("\"trace_id\":\"trace-1\"") != std::string::npos);
	TEST_REQUIRE(record.find("quoted \\\"message\\\"") != std::string::npos);
	TEST_REQUIRE(record.find("\"result\":\"ok\"") != std::string::npos);
	return true;
}

bool rejected_insertion_throws_explicit_failure()
{
	RejectingStreamBuffer buffer;
	std::ostream output(&buffer);
	bhp::StructuredLogger logger("structured_logger_test", output);
	try {
		logger.log(bhp::StructuredLogLevel::info, "message", "trace-2");
	} catch (const std::runtime_error &error) {
		TEST_REQUIRE(std::string(error.what()) ==
			     "STRUCTURED_LOGGING.OUTPUT_STREAM_WRITE_FAILED");
		return true;
	}
	return false;
}

bool rejected_flush_throws_explicit_failure()
{
	FlushFailingStreamBuffer buffer;
	std::ostream output(&buffer);
	bhp::StructuredLogger logger("structured_logger_test", output);
	try {
		logger.log_error("message", "trace-3", "TEST.ERROR", "failure", "none");
	} catch (const std::runtime_error &error) {
		TEST_REQUIRE(std::string(error.what()) ==
			     "STRUCTURED_LOGGING.OUTPUT_STREAM_WRITE_FAILED");
		return true;
	}
	return false;
}

using TestFunction = bool (*)();

} // namespace

int main()
{
	const std::array<std::pair<std::string_view, TestFunction>, 3> tests{{
		{"successful record", successful_record_is_complete_json_line},
		{"insertion failure", rejected_insertion_throws_explicit_failure},
		{"flush failure", rejected_flush_throws_explicit_failure},
	}};
	for (const auto &[name, test] : tests) {
		if (!test()) {
			std::cerr << "FAILED: " << name << '\n';
			return 1;
		}
	}
	std::cout << "structured_logger_test: " << tests.size() << " passed\n";
	return 0;
}
