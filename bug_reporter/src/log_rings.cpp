#include "bug_reporter/log_rings.h"
#include <algorithm>
#include <cerrno>
#include <fcntl.h>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

namespace bug_reporter
{
LogRings::LogRings(std::size_t bytes_per_service, std::size_t maximum_services, std::size_t maximum_line_bytes)
	: bytes_per_service_(bytes_per_service), maximum_services_(maximum_services),
	  maximum_line_bytes_(maximum_line_bytes)
{
}

void LogRings::append(const std::string &service, std::string line, std::chrono::steady_clock::time_point at)
{
	auto found = rings_.find(service);
	if (found == rings_.end()) {
		if (rings_.size() >= maximum_services_) {
			++dropped_;
			return;
		}
		found = rings_.emplace(service, Ring{}).first;
	}
	if (line.size() > maximum_line_bytes_) {
		line.resize(maximum_line_bytes_);
		line += " [cut]";
	}
	auto &ring = found->second;
	ring.bytes += line.size();
	ring.lines.push_back({next_sequence_++, at, service, std::move(line)});
	while (ring.bytes > bytes_per_service_ && ring.lines.size() > 1) {
		ring.bytes -= ring.lines.front().line.size();
		ring.lines.pop_front();
	}
}

std::vector<LogLine> LogRings::window(std::chrono::steady_clock::time_point since) const
{
	std::vector<LogLine> lines;
	for (const auto &[service, ring] : rings_)
		for (const auto &line : ring.lines)
			if (line.at >= since)
				lines.push_back(line);
	std::sort(lines.begin(), lines.end(),
		  [](const LogLine &a, const LogLine &b) { return a.sequence < b.sequence; });
	return lines;
}

std::string FaultRing::path(int half) const
{
	return directory_ + "/fault-" + std::to_string(half) + ".jsonl";
}

Result<FaultRing> FaultRing::open(std::string directory, std::size_t total_bytes)
{
	using Output = Result<FaultRing>;
	if (::mkdir(directory.c_str(), 0700) != 0 && errno != EEXIST)
		return Output::failure({"REPORT.STORAGE_UNAVAILABLE", "Cannot create the fault ring's directory", {}, false});
	struct stat folder {};
	if (::stat(directory.c_str(), &folder) != 0 || !S_ISDIR(folder.st_mode) || folder.st_uid != ::geteuid() ||
	    (folder.st_mode & 077) != 0)
		return Output::failure({"REPORT.STORAGE_UNAVAILABLE", "The fault ring's directory is not owner-only", {}, false});
	// The current half is the one written last.
	struct stat first {}, second {};
	const bool has_first = ::stat((directory + "/fault-0.jsonl").c_str(), &first) == 0;
	const bool has_second = ::stat((directory + "/fault-1.jsonl").c_str(), &second) == 0;
	int current = 0;
	if (has_second && (!has_first || second.st_mtim.tv_sec > first.st_mtim.tv_sec ||
			   (second.st_mtim.tv_sec == first.st_mtim.tv_sec && second.st_mtim.tv_nsec > first.st_mtim.tv_nsec)))
		current = 1;
	return Output::success(FaultRing(std::move(directory), std::max<std::size_t>(total_bytes / 2, 1024), current));
}

Result<bool> FaultRing::append(const std::string &line)
{
	const auto failed = [] {
		return Result<bool>::failure({"REPORT.STORAGE_UNAVAILABLE", "The fault ring could not be written", {}, true});
	};
	struct stat current {};
	if (::stat(path(current_).c_str(), &current) == 0 &&
	    static_cast<std::size_t>(current.st_size) + line.size() + 1 > half_bytes_) {
		current_ = 1 - current_;
		const int emptied = ::open(path(current_).c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
		if (emptied < 0)
			return failed();
		::close(emptied);
	}
	const int file = ::open(path(current_).c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
	if (file < 0)
		return failed();
	const auto text = line + "\n";
	const bool written = ::write(file, text.data(), text.size()) == static_cast<ssize_t>(text.size()) &&
			     ::fdatasync(file) == 0;
	::close(file);
	return written ? Result<bool>::success(true) : failed();
}

Result<std::vector<std::string>> FaultRing::read() const
{
	std::vector<std::string> lines;
	for (const int half : {1 - current_, current_}) {
		std::ifstream input(path(half));
		for (std::string line; std::getline(input, line);)
			if (!line.empty())
				lines.push_back(line);
	}
	return Result<std::vector<std::string>>::success(std::move(lines));
}
} // namespace bug_reporter
