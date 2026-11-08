#pragma once

#include "bhp/structured_log_level.h"

#include <map>
#include <ostream>
#include <string>

namespace bhp {

/**
 * @canonical structured_logging -- all user-space JSON logs use this class.
 *
 * Values supplied in context_fields must already be safe to log. This class
 * guarantees encoding, not semantic redaction.
 */
class StructuredLogger final {
public:
	StructuredLogger(std::string service_name, std::ostream &output_stream);

	/** Writes and flushes one JSON record; throws when insertion or flush fails. */
	void log(StructuredLogLevel level,
		 const std::string &message,
		 const std::string &trace_id,
		 const std::map<std::string, std::string> &context_fields = {});

	/** Adds the canonical error fields and delegates to the same checked write. */
	void log_error(const std::string &message,
		       const std::string &trace_id,
		       const std::string &error_code,
		       const std::string &error_message,
		       const std::string &stack_trace,
		       const std::map<std::string, std::string> &context_fields = {});

	/**
	 * A fault: an error record that means a bug, which the bug reporter files
	 * automatically (bug-reports-and-self-repair.md). The same fields as
	 * log_error plus `"reportable":"true"` and the call site in `location`.
	 * Expected failures (a network down, a device asleep) use log_error.
	 */
	void fault(const std::string &message,
		   const std::string &trace_id,
		   const std::string &error_code,
		   const std::string &error_message,
		   const std::string &location,
		   const std::map<std::string, std::string> &context_fields = {});

private:
	std::string service_name_;
	std::ostream &output_stream_;
};

} // namespace bhp
