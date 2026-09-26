// Check-only capture-closure run for simulation evidence. It evaluates the same
// CaptureClosureGatedSuspend the daemon uses, but its inner suspend never writes
// /sys/power/state. Output is one structured JSON log line; exit 0 means verified,
// 1 means refused, 2 means invalid arguments.

#include "panel_supervisor/capture_closure.hpp"
#include "panel_supervisor/error_code.hpp"
#include "panel_supervisor/supervisor_error.hpp"

#include "bhp/structured_log_level.h"
#include "bhp/structured_logger.h"

#include <charconv>
#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace {

using namespace house_panel::supervisor;

constexpr const char *kTraceId = "capture-closure-check";

class CheckOnlySuspend final : public Suspend {
public:
	Result<void> request_suspend() override
	{
		++calls;
		return Result<void>::success();
	}
	int calls{0};
};

std::optional<std::uint32_t> parse_uid(const std::string_view text)
{
	unsigned long long value = 0;
	const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
	if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || value == 0 ||
	    value > std::numeric_limits<std::uint32_t>::max()) {
		return std::nullopt;
	}
	return static_cast<std::uint32_t>(value);
}

std::string error_chain(const SupervisorError &error)
{
	std::string chain(to_string(error.code()));
	for (auto cause = error.cause(); cause != nullptr; cause = cause->cause()) {
		chain += " <- ";
		chain += to_string(cause->code());
	}
	return chain;
}

} // namespace

int main(const int argc, char **argv)
{
	bhp::StructuredLogger logger("beaglebone_house_panel.capture_closure_check", std::cout);
	std::optional<std::uint32_t> broker_uid;
	std::string proc_root = "/proc";
	for (int index = 1; index < argc; ++index) {
		const std::string_view argument = argv[index];
		if (argument == "--media-broker-uid" && index + 1 < argc) {
			broker_uid = parse_uid(argv[++index]);
			if (!broker_uid) {
				logger.log_error("capture_closure_check_usage_invalid", kTraceId,
						 "CAPTURE_CLOSURE_CHECK.USAGE_INVALID",
						 "--media-broker-uid requires a decimal UID from 1 to 4294967295",
						 "not_available_explicit_error_return",
						 {{"argument", "--media-broker-uid"}});
				return 2;
			}
		} else if (argument == "--proc-root" && index + 1 < argc) {
			proc_root = argv[++index];
		} else {
			logger.log_error("capture_closure_check_usage_invalid", kTraceId,
					 "CAPTURE_CLOSURE_CHECK.USAGE_INVALID",
					 "usage: capture_closure_check [--media-broker-uid UID] [--proc-root DIR]",
					 "not_available_explicit_error_return",
					 {{"argument_content", "redacted"}});
			return 2;
		}
	}

	const auto bounds = validate_capture_closure_bounds(CaptureClosureBounds{});
	if (!bounds) {
		return 2;
	}
	ProcfsCaptureClosureProbe probe(proc_root);
	std::unique_ptr<UidScopedMediaTerminator> terminator;
	if (broker_uid) {
		terminator = std::make_unique<UidScopedMediaTerminator>(*broker_uid, proc_root);
	}
	CheckOnlySuspend check_only;
	CaptureClosureGatedSuspend gate(check_only, probe, terminator.get(), bounds.value());
	const auto result = gate.request_suspend();
	const CaptureClosureReport &report = gate.last_report();
	std::map<std::string, std::string> context{
		{"evidence", "simulation_not_hardware_evidence"},
		{"forced", report.forced ? "true" : "false"},
		{"initial_holders", std::to_string(report.initial_holders)},
		{"initial_open_substreams", std::to_string(report.initial_open_substreams)},
		{"initial_unreadable_processes", std::to_string(report.initial_unreadable_processes)},
		{"media_termination_path", terminator ? "uid_scoped" : "unavailable"},
		{"suspend_write", "not_performed_check_only"},
		{"termination_requested", report.termination_requested ? "true" : "false"},
		{"verified", report.verified ? "true" : "false"},
		{"waited_milliseconds", std::to_string(report.waited.count())},
	};
	if (result && check_only.calls == 1) {
		logger.log(bhp::StructuredLogLevel::info, "capture_closure_verified", kTraceId, context);
		return 0;
	}
	if (result) {
		return 1;
	}
	for (const auto &[key, value] : result.error().context()) {
		context.emplace("error_context_" + key, value);
	}
	logger.log_error("capture_closure_refused", kTraceId,
			 std::string(to_string(result.error().code())), result.error().message(),
			 error_chain(result.error()), context);
	return 1;
}
