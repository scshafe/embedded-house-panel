// ReporterService end to end against the in-memory inbox: faults, exits and
// manual reports become bundles, reach the inbox, and their status comes back.
#include "fake_inbox.hpp"
#include "bug_reporter/reporter_service.h"
#include <fstream>
#include <iterator>
#include <sys/stat.h>
using namespace bug_reporter;
using namespace bug_reporter_test;
namespace wire = bug_reporter::protocol;
namespace
{
struct Rig {
	FakeInbox inbox;
	std::chrono::steady_clock::time_point steady{std::chrono::hours(1)};
	std::int64_t unix_seconds{1'794'000'000};
	unsigned char counter{0};
	std::string root{temporary_directory("bhp-reporter")};
	std::unique_ptr<ReporterService> service;
	Rig()
	{
		std::ofstream(root + "/image-version") << "board_id=frdm-imx8mp\ncommit=abc123\n";
		ReporterSettings settings;
		settings.outbox_directory = root + "/outbox";
		settings.fault_directory = root + "/faults";
		settings.build_info_path = root + "/image-version";
		settings.inbox.base_url = inbox.base;
		ReporterPorts ports;
		ports.transport = std::ref(inbox);
		ports.steady_now = [this] { return steady; };
		ports.unix_now = [this] { return unix_seconds; };
		ports.random01 = [] { return 0.5; };
		ports.random_bytes = [this](unsigned char *bytes, std::size_t size) {
			for (std::size_t i = 0; i < size; ++i)
				bytes[i] = ++counter;
			return true;
		};
		::mkdir(settings.outbox_directory.c_str(), 0700);
		auto opened = ReporterService::open(settings, ports);
		if (!opened.has_value()) {
			std::cerr << opened.error().code << '\n';
			std::exit(2);
		}
		service = std::move(opened.value());
	}
	void advance(std::chrono::seconds by)
	{
		steady += by;
		unix_seconds += by.count();
	}
};

const std::string kFault =
	R"({"timestamp":"2026-11-08T12:00:00Z","level":"error","message":"queue order lost","service":"music","trace_id":"t-9","error_code":"MUSIC.INTERNAL","error_message":"x","stack_trace":"","reportable":"true","location":"music_service.cpp:tick"})";

void a_fault_reaches_the_inbox()
{
	Rig rig;
	rig.service->ingest("music", R"({"level":"info","message":"started","trace_id":"t-1"})");
	rig.service->ingest("music", "a plain line");
	rig.service->ingest("music", kFault);
	const auto listed = rig.service->reports();
	expect(listed.items.size() == 1 && listed.items[0].kind == "fault" && listed.items[0].status == "queued" &&
		       listed.items[0].created_on == "2026-11-06",
	       "a fault files an automatic report");
	(void)rig.service->tick();
	expect(rig.inbox.completed.size() == 1, "the bundle reaches the inbox");
	const auto &manifest = rig.inbox.completed.begin()->second;
	expect(manifest["component"] == "music" && manifest["error_code"] == "MUSIC.INTERNAL" &&
		       manifest["location"] == "music_service.cpp:tick" && manifest["build"]["commit"] == "abc123",
	       "the manifest names the component, code, call site and build");
	expect(manifest["trace_ids"] == Json::array({"t-1", "t-9"}), "with the window's trace IDs");
	expect(rig.service->reports().items[0].status == "received", "the inbox's acknowledgement shows");
	const auto id = rig.service->reports().items[0].report_id;
	struct stat gone {};
	expect(::stat((rig.root + "/outbox/" + id).c_str(), &gone) != 0, "a received bundle leaves the panel");

	// Repeats within a day are counts, sent as one small update.
	rig.service->ingest("music", kFault);
	rig.service->ingest("music", kFault);
	(void)rig.service->tick();
	expect(rig.inbox.completed.size() == 1 && rig.inbox.counts.size() == 1 && rig.inbox.counts.begin()->second == 2,
	       "two repeats are one count update");

	rig.inbox.statuses[id] = {"fix_in_pr", 81};
	const auto before = rig.service->reports_sequence();
	rig.advance(std::chrono::minutes(16));
	(void)rig.service->tick();
	const auto after = rig.service->reports();
	expect(after.items[0].status == "fix_in_pr" && after.items[0].pr == 81u && rig.service->reports_sequence() > before,
	       "the status poll brings the PR back");
	rig.inbox.statuses[id] = {"update_ready", 81};
	rig.advance(std::chrono::minutes(16));
	(void)rig.service->tick();
	expect(rig.service->reports().update_ready, "an update ready shows");
}

void exits_and_backoff()
{
	Rig rig;
	rig.inbox.unreachable = true;
	rig.service->exited({"bhp-panel-ui", 0, 11, true});
	rig.service->exited({"bhp-media-broker", 3, 0, true});
	rig.service->exited({"bhp-media-broker", 0, 0, false});
	auto listed = rig.service->reports();
	expect(listed.items.size() == 2 && listed.items[1].kind == "crash" && listed.items[0].kind == "restart",
	       "a signal is a crash, a status a restart, and a clean exit nothing");
	const auto wait = rig.service->tick();
	expect(rig.service->last_error() == "REPORT.INBOX_UNREACHABLE" && wait <= std::chrono::seconds(5),
	       "an unreachable inbox backs off");
	rig.inbox.unreachable = false;
	rig.advance(std::chrono::seconds(5));
	(void)rig.service->tick();
	(void)rig.service->tick();
	expect(rig.inbox.completed.size() == 2, "both reach the inbox once it answers");
	bool crash = false;
	for (const auto &[id, manifest] : rig.inbox.completed)
		crash = crash || (manifest["kind"] == "crash" && manifest["error_code"] == "PROCESS.SIGNAL_11" &&
				  manifest["component"] == "bhp-panel-ui");
	expect(crash, "the crash names its signal and service");
	std::ifstream ring(rig.root + "/faults/fault-0.jsonl");
	const std::string faults{std::istreambuf_iterator<char>(ring), std::istreambuf_iterator<char>()};
	expect(faults.find("\"signal\":11") != std::string::npos, "the exit is in the persistent fault ring");
}

void manual_reports()
{
	Rig rig;
	rig.service->ingest("bhp-panel-ui", R"({"level":"info","message":"tap","trace_id":"t-5"})");
	auto taken = rig.service->handle(wire::SnapshotRequest{{"music", "playing", "day_active", "present", "day", "active"}});
	expect(taken.size() == 1 && std::holds_alternative<wire::SnapshotTaken>(taken[0]), "a snapshot");
	const auto snapshot = std::get<wire::SnapshotTaken>(taken[0]).snapshot_id;
	// A line after the tap is not part of that report's context.
	rig.service->ingest("bhp-panel-ui", R"({"level":"info","message":"later","trace_id":"t-6"})");
	auto submitted = rig.service->handle(wire::SubmitRequest{snapshot, "Skip did nothing."});
	expect(submitted.size() == 2 && std::holds_alternative<wire::Submitted>(submitted[0]), "submitted");
	(void)rig.service->tick();
	expect(rig.inbox.completed.size() == 1, "it reaches the inbox");
	const auto &manifest = rig.inbox.completed.begin()->second;
	expect(manifest["kind"] == "text" && manifest["screen"]["surface"] == "music" &&
		       manifest["description_text"] == "Skip did nothing." && manifest["trace_ids"] == Json::array({"t-5"}),
	       "the screen, text and logs of the moment of the tap");
	auto again = rig.service->handle(wire::SubmitRequest{snapshot, "again"});
	expect(std::holds_alternative<wire::Refused>(again[0]) &&
		       std::get<wire::Refused>(again[0]).error_code == "REPORT.SNAPSHOT_EXPIRED",
	       "a snapshot files once");
	auto second = rig.service->handle(wire::SnapshotRequest{{"home", "", "day_active", "present", "day", "active"}});
	auto limited = rig.service->handle(wire::SubmitRequest{std::get<wire::SnapshotTaken>(second[0]).snapshot_id, ""});
	expect(std::holds_alternative<wire::Refused>(limited[0]) &&
		       std::get<wire::Refused>(limited[0]).error_code == "REPORT.RATE_LIMITED",
	       "one manual report a minute, said on the screen");
	auto third = rig.service->handle(wire::SnapshotRequest{{"home", "", "day_active", "present", "day", "active"}});
	rig.advance(std::chrono::minutes(11));
	auto stale = rig.service->handle(wire::SubmitRequest{std::get<wire::SnapshotTaken>(third[0]).snapshot_id, ""});
	expect(std::holds_alternative<wire::Refused>(stale[0]), "a snapshot expires after 10 minutes");
}
} // namespace

int main()
{
	a_fault_reaches_the_inbox();
	exits_and_backoff();
	manual_reports();
	if (failures == 0)
		std::cout << "bug_reporter_reporter_service_test passed\n";
	return failures == 0 ? 0 : 1;
}
