#pragma once
#include "bug_reporter/result.h"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

/**
 * Where the panel's logs live (bug-reports-and-self-repair.md, section 3):
 * routine lines only in RAM, the eMMC only for faults.
 */
namespace bug_reporter
{
struct LogLine {
	std::uint64_t sequence{0};
	std::chrono::steady_clock::time_point at;
	std::string service;
	std::string line;
};

/**
 * @canonical bug_reporter_log_rings -- one byte-bounded RAM ring per service.
 *
 * Each ring keeps the newest whole lines within `bytes_per_service`; a line
 * longer than `maximum_line_bytes` is cut and marked. At most
 * `maximum_services` rings exist; lines from further services are dropped and
 * counted. `window` merges every ring in arrival order.
 */
class LogRings final
{
public:
	explicit LogRings(std::size_t bytes_per_service = 512 * 1024, std::size_t maximum_services = 32,
			  std::size_t maximum_line_bytes = 16 * 1024);
	void append(const std::string &service, std::string line, std::chrono::steady_clock::time_point at);
	/** Every kept line that arrived at or after `since`, oldest first. */
	std::vector<LogLine> window(std::chrono::steady_clock::time_point since) const;
	std::uint64_t dropped() const { return dropped_; }

private:
	struct Ring {
		std::deque<LogLine> lines;
		std::size_t bytes{0};
	};
	std::size_t bytes_per_service_;
	std::size_t maximum_services_;
	std::size_t maximum_line_bytes_;
	std::map<std::string, Ring> rings_;
	std::uint64_t next_sequence_{0};
	std::uint64_t dropped_{0};
};

/**
 * @canonical bug_reporter_fault_ring -- the small persistent ring of faults.
 *
 * Two halves, `fault-0.jsonl` and `fault-1.jsonl`, in an owner-only
 * directory, `total_bytes` in all. Each fault or exit record is appended and
 * flushed with fdatasync, so a crash that takes the reporter down is still
 * there after a reboot. When the current half is full, the other half is
 * emptied and becomes current. Only faults are written: routine logs never
 * touch the eMMC.
 */
class FaultRing final
{
public:
	/** REPORT.STORAGE_UNAVAILABLE when the directory cannot be made owner-only. */
	static Result<FaultRing> open(std::string directory, std::size_t total_bytes = 256 * 1024);
	Result<bool> append(const std::string &line);
	/** Older half first, then the current one. */
	Result<std::vector<std::string>> read() const;

private:
	FaultRing(std::string directory, std::size_t half_bytes, int current)
		: directory_(std::move(directory)), half_bytes_(half_bytes), current_(current)
	{
	}
	std::string path(int half) const;
	std::string directory_;
	std::size_t half_bytes_;
	int current_;
};
} // namespace bug_reporter
