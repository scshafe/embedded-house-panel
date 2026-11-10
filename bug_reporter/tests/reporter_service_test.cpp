// ReporterService end to end against the in-memory inbox: faults, exits and
// manual reports become bundles, reach the inbox, and their status comes back.
#include "fake_inbox.hpp"
#include "bug_reporter/reporter_service.h"
#include <filesystem>
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
		settings.handoff_directory = root + "/handoff";
		::mkdir(settings.handoff_directory.c_str(), 0770);
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

void wrong_types_never_end_the_reporter()
{
	Rig rig;
	try {
		for (const auto *line : {R"({"level":"error","reportable":true,"error_code":"MUSIC.INTERNAL"})",
					 R"({"level":5,"reportable":"true","error_code":"MUSIC.INTERNAL"})",
					 R"({"type":7,"service":"music"})",
					 R"({"level":"error","reportable":"true","error_code":"MUSIC.INTERNAL","location":{"line":3}})"})
			rig.service->ingest("music", line);
	} catch (const std::exception &) {
		expect(false, "a log line of the wrong types never throws");
	}
	const auto listed = rig.service->reports();
	expect(listed.items.size() == 1 && listed.items[0].kind == "fault",
	       "only the well-typed fault files, its odd location treated as absent");
}

void a_refused_bundle_does_not_block_the_rest()
{
	Rig rig;
	rig.service->exited({"bhp-panel-ui", 0, 11, true});
	rig.service->exited({"bhp-media-broker", 3, 0, true});
	const auto first = rig.service->reports().items[1].report_id;
	rig.inbox.refuse.insert(first);
	(void)rig.service->tick();
	(void)rig.service->tick();
	const auto listed = rig.service->reports();
	expect(rig.inbox.completed.size() == 1 && !rig.inbox.completed.count(first), "the one behind it still goes");
	expect(listed.items[1].status == "refused" && rig.service->last_error() == "REPORT.INBOX_REFUSED",
	       "the refused one is parked and shown as refused");
	struct stat gone {};
	expect(::stat((rig.root + "/outbox/" + first).c_str(), &gone) != 0, "its bundle leaves the panel");

	// A 403 is about the panel, so the bundle waits instead.
	rig.service->exited({"bhp-panel-ui", 0, 6, true});
	rig.inbox.forbidden = 100;
	rig.advance(std::chrono::minutes(2));
	(void)rig.service->tick();
	expect(rig.service->reports().items[0].status == "queued", "a forbidden panel keeps its report queued");
}

void counts_add_once_and_never_block()
{
	Rig rig;
	const std::string other = R"({"level":"error","reportable":"true","error_code":"MUSIC.TIMEOUT","location":"x.cpp:1"})";
	for (const auto &line : {kFault, other})
		rig.service->ingest("music", line);
	(void)rig.service->tick();
	(void)rig.service->tick();
	expect(rig.inbox.completed.size() == 2, "two full reports");
	for (int i = 0; i < 2; ++i)
		for (const auto &line : {kFault, other})
			rig.service->ingest("music", line);
	rig.inbox.lose_next_count_answer = true;
	rig.advance(std::chrono::minutes(2));
	(void)rig.service->tick();
	rig.advance(std::chrono::minutes(2));
	(void)rig.service->tick();
	std::uint64_t total = 0;
	for (const auto &[fingerprint, count] : rig.inbox.counts)
		total += count;
	expect(total == 4, "a count update whose answer was lost is added once, not twice");

	// A refused update is dropped and never holds back another fingerprint's.
	const auto refused = rig.inbox.counts.begin()->first;
	rig.inbox.refuse.insert(refused);
	for (const auto &line : {kFault, other})
		rig.service->ingest("music", line);
	rig.advance(std::chrono::minutes(2));
	(void)rig.service->tick();
	total = 0;
	for (const auto &[fingerprint, count] : rig.inbox.counts)
		total += count;
	expect(total == 5, "the other fingerprint's count still arrives");
	(void)rig.service->tick();
	expect(rig.service->tick() > std::chrono::seconds(30), "nothing is left to retry");
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
	auto submitted = rig.service->handle(wire::SubmitRequest{snapshot, "Skip did nothing.", std::nullopt});
	expect(submitted.size() == 2 && std::holds_alternative<wire::Submitted>(submitted[0]), "submitted");
	(void)rig.service->tick();
	expect(rig.inbox.completed.size() == 1, "it reaches the inbox");
	const auto &manifest = rig.inbox.completed.begin()->second;
	expect(manifest["kind"] == "text" && manifest["screen"]["surface"] == "music" &&
		       manifest["description_text"] == "Skip did nothing." && manifest["trace_ids"] == Json::array({"t-5"}),
	       "the screen, text and logs of the moment of the tap");
	auto again = rig.service->handle(wire::SubmitRequest{snapshot, "again", std::nullopt});
	expect(std::holds_alternative<wire::Refused>(again[0]) &&
		       std::get<wire::Refused>(again[0]).error_code == "REPORT.SNAPSHOT_EXPIRED",
	       "a snapshot files once");
	auto second = rig.service->handle(wire::SnapshotRequest{{"home", "", "day_active", "present", "day", "active"}});
	auto limited = rig.service->handle(wire::SubmitRequest{std::get<wire::SnapshotTaken>(second[0]).snapshot_id, "", std::nullopt});
	expect(std::holds_alternative<wire::Refused>(limited[0]) &&
		       std::get<wire::Refused>(limited[0]).error_code == "REPORT.RATE_LIMITED",
	       "one manual report a minute, said on the screen");
	auto third = rig.service->handle(wire::SnapshotRequest{{"home", "", "day_active", "present", "day", "active"}});
	rig.advance(std::chrono::minutes(11));
	auto stale = rig.service->handle(wire::SubmitRequest{std::get<wire::SnapshotTaken>(third[0]).snapshot_id, "", std::nullopt});
	expect(std::holds_alternative<wire::Refused>(stale[0]), "a snapshot expires after 10 minutes");
}
// A broker take as the hand-off leaves it: 6 channels of 16-bit PCM, channel n carrying n+1.
std::string write_take(const std::string &handoff, const std::string &purpose, std::uint32_t frames)
{
	const std::string id = "take-" + std::string(63, 'c') + std::to_string(frames % 10);
	const auto directory = handoff + "/" + id;
	::mkdir(directory.c_str(), 0770);
	std::string wav = "RIFF";
	const auto put32 = [&wav](std::uint32_t v) { for (int s = 0; s < 32; s += 8) wav += static_cast<char>((v >> s) & 0xff); };
	const auto put16 = [&wav](std::uint16_t v) { wav += static_cast<char>(v & 0xff); wav += static_cast<char>(v >> 8); };
	put32(36 + frames * 12);
	wav += "WAVEfmt ";
	put32(16); put16(1); put16(6); put32(16000); put32(16000 * 12); put16(12); put16(16);
	wav += "data";
	put32(frames * 12);
	for (std::uint32_t f = 0; f < frames; ++f)
		for (std::uint16_t channel = 0; channel < 6; ++channel)
			put16(static_cast<std::uint16_t>(channel + 1));
	std::ofstream(directory + "/original.wav", std::ios::binary) << wav;
	std::ofstream(directory + "/capture.json") << Json({{"purpose", purpose}}).dump();
	return id;
}

void voice_reports()
{
	Rig rig;
	const auto snapshot = [&rig] {
		auto taken = rig.service->handle(wire::SnapshotRequest{{"study", "front", "day_active", "present", "day", "active"}});
		return std::get<wire::SnapshotTaken>(taken[0]).snapshot_id;
	};
	const auto take = write_take(rig.root + "/handoff", "bug_report", 2000);
	auto submitted = rig.service->handle(wire::SubmitRequest{snapshot(), "", take});
	expect(std::holds_alternative<wire::Submitted>(submitted[0]), "a spoken description files a voice report");
	struct stat gone {};
	expect(::stat((rig.root + "/handoff/" + take).c_str(), &gone) != 0, "the hand-off take is deleted once used");
	(void)rig.service->tick();
	expect(rig.inbox.completed.size() == 1 && rig.inbox.completed.begin()->second["kind"] == "voice", "it reaches the inbox");
	const auto &audio = rig.inbox.files.begin()->second["description.wav"];
	expect(audio.size() == 44 + 2000 * 2 && audio[22] == 1 && audio[44] == 1 && audio[46] == 1,
	       "as a mono WAV of channel 0");

	rig.advance(std::chrono::minutes(2));
	const auto voice_take = write_take(rig.root + "/handoff", "voice_take", 3000);
	auto refused = rig.service->handle(wire::SubmitRequest{snapshot(), "", voice_take});
	expect(std::holds_alternative<wire::Refused>(refused[0]) &&
		       std::get<wire::Refused>(refused[0]).error_code == "REPORT.AUDIO_INVALID",
	       "a voice-lab take is never a bug description");
	auto missing = rig.service->handle(wire::SubmitRequest{snapshot(), "", "take-" + std::string(64, 'd')});
	expect(std::holds_alternative<wire::Refused>(missing[0]), "a missing take is refused");

	// A take nobody submitted is purged after the snapshot lifetime.
	const auto stale = write_take(rig.root + "/handoff", "bug_report", 4001);
	const auto old = std::filesystem::file_time_type::clock::now() - std::chrono::minutes(11);
	std::filesystem::last_write_time(rig.root + "/handoff/" + stale, old);
	(void)rig.service->tick();
	expect(!std::filesystem::exists(rig.root + "/handoff/" + stale), "an unused take does not linger");
}
} // namespace

int main()
{
	wrong_types_never_end_the_reporter();
	a_refused_bundle_does_not_block_the_rest();
	counts_add_once_and_never_block();
	a_fault_reaches_the_inbox();
	exits_and_backoff();
	manual_reports();
	voice_reports();
	if (failures == 0)
		std::cout << "bug_reporter_reporter_service_test passed\n";
	return failures == 0 ? 0 : 1;
}
