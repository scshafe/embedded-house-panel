#pragma once
#include <optional>
#include <string>
#include <variant>

/**
 * @canonical bug_reporter_ingest -- what `bhp-log-tee` sends the reporter.
 *
 * Each service's stderr runs through `bhp-log-tee`, which copies every line to
 * its own stdout and to the reporter's ingest socket. A connection opens with
 * `{"type":"hello","service":NAME}`; every following line is a log line, except
 * `{"type":"exit",...}`, which the init scripts' restart wrapper sends (through
 * `bhp-log-tee --exit-status`) when a service ends without being asked to.
 */
namespace bug_reporter
{
struct HelloLine {
	std::string service;
};

struct ExitLine {
	std::string service;
	/** The exit status, or 0 when a signal ended it. */
	int status{0};
	/** The signal that ended it, or 0. */
	int signal{0};
	/** The wrapper restarts it. */
	bool restarting{false};
};

struct LogRecord {
	std::string line;
	/** A `fault()` record: its code and call site. */
	std::optional<std::string> fault_code;
	std::string fault_location;
};

using IngestLine = std::variant<HelloLine, ExitLine, LogRecord>;

/** A service name: `[a-z0-9_.-]{1,64}`. */
bool valid_service(const std::string &service);
/**
 * Classifies one line (without its newline). A line that is not JSON is still
 * a log line: third-party libraries write plain text to stderr too. A fault
 * needs `"level":"error"`, `"reportable":"true"` and a dotted upper-case
 * `error_code`; anything else is an ordinary line.
 */
IngestLine classify(const std::string &line);
std::string encode_hello(const std::string &service);
std::string encode_exit(const ExitLine &exit);
} // namespace bug_reporter
