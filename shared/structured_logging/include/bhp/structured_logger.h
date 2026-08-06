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

private:
	std::string service_name_;
	std::ostream &output_stream_;
};

} // namespace bhp
