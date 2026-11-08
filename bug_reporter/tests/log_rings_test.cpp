// The RAM log rings and the persistent fault ring.
#include "bug_reporter/log_rings.h"
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>
using namespace bug_reporter;
namespace
{
int failures = 0;
void expect(bool value, const std::string &label)
{
	if (!value) {
		++failures;
		std::cerr << "FAIL: " << label << '\n';
	}
}

void rings()
{
	const auto start = std::chrono::steady_clock::time_point(std::chrono::hours(1));
	LogRings logs(100, 2, 40);
	logs.append("ui", std::string(30, 'a'), start);
	logs.append("broker", "b1", start + std::chrono::seconds(1));
	logs.append("ui", std::string(30, 'c'), start + std::chrono::seconds(2));
	logs.append("ui", std::string(50, 'd'), start + std::chrono::seconds(3));
	auto all = logs.window(start);
	expect(all.size() == 3 && all.front().service == "broker" && all.back().line == std::string(40, 'd') + " [cut]",
	       "a ring keeps its newest lines within its bytes, and long lines are cut");
	expect(all[1].line == std::string(30, 'c'), "the merged window is in arrival order");
	logs.append("third", "x", start + std::chrono::seconds(4));
	expect(logs.dropped() == 1, "a service beyond the limit is dropped and counted");
	expect(logs.window(start + std::chrono::seconds(3)).size() == 1, "the window starts at its time");
}

void fault_ring()
{
	char pattern[] = "/tmp/bhp-fault-ring-XXXXXX";
	const std::string root = ::mkdtemp(pattern);
	auto opened = FaultRing::open(root + "/faults", 2048);
	expect(opened.has_value(), "the ring opens");
	if (!opened.has_value())
		return;
	auto ring = opened.value();
	struct stat folder {};
	::stat((root + "/faults").c_str(), &folder);
	expect((folder.st_mode & 0777) == 0700, "its directory is 0700");
	for (int i = 0; i < 40; ++i)
		expect(ring.append("{\"n\":" + std::to_string(i) + ",\"pad\":\"" + std::string(40, 'p') + "\"}").has_value(),
		       "append");
	auto lines = ring.read();
	expect(lines.has_value() && !lines.value().empty() && lines.value().back().find("\"n\":39") != std::string::npos,
	       "the newest fault is last");
	expect(lines.value().front().find("\"n\":0,") == std::string::npos, "the oldest faults rotated out");
	std::size_t bytes = 0;
	for (const auto &line : lines.value())
		bytes += line.size() + 1;
	expect(bytes <= 2048, "the ring stays within its bytes");
	auto reopened = FaultRing::open(root + "/faults", 2048);
	expect(reopened.has_value() && reopened.value().read().value() == lines.value(), "a reboot finds the same faults");
	::chmod((root + "/faults").c_str(), 0755);
	expect(!FaultRing::open(root + "/faults").has_value(), "a directory others can read is refused");
}
} // namespace

int main()
{
	rings();
	fault_ring();
	if (failures == 0)
		std::cout << "bug_reporter_log_rings_test passed\n";
	return failures == 0 ? 0 : 1;
}
