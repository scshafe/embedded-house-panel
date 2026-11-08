// The reporter frames: closed key sets both ways, bounds, and the client's UID check.
#include "bug_reporter/reporter_protocol.h"
#include <iostream>
#include <sys/socket.h>
#include <unistd.h>
using namespace bug_reporter::protocol;
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

const std::string kSnapshot = "snap-0123456789abcdef";
const std::string kReport = "rpt-0123456789abcdef0123456789abcdef";

void requests()
{
	const UiRequest snapshot = SnapshotRequest{{"music", "playing", "day_active", "present", "day", "active"}};
	for (const UiRequest &request :
	     {snapshot, UiRequest(SubmitRequest{kSnapshot, "Skip did nothing.\nTwice.", std::nullopt}),
	      UiRequest(SubmitRequest{kSnapshot, "", "take-" + std::string(64, 'a')}), UiRequest(CancelRequest{kSnapshot}),
	      UiRequest(StatusRequest{})}) {
		auto frame = encode(request);
		expect(frame.has_value() && decode_ui_request(frame.value()).has_value() &&
			       decode_ui_request(frame.value()).value().index() == request.index(),
		       "a request round trips");
	}
	expect(!encode(UiRequest(SnapshotRequest{{"Music", "", "", "", "", ""}})).has_value(), "screen fields are tokens");
	expect(!encode(UiRequest(SubmitRequest{"snap-1", "", std::nullopt})).has_value(), "a snapshot ID has its form");
	expect(!encode(UiRequest(SubmitRequest{kSnapshot, std::string(2001, 'x'), std::nullopt})).has_value(), "text is bounded");
	expect(!encode(UiRequest(SubmitRequest{kSnapshot, "a\tb", std::nullopt})).has_value(), "no control characters but newlines");
	expect(!encode(UiRequest(SubmitRequest{kSnapshot, "", "take-../x"})).has_value(), "a take ID has its form");
	auto spoken = decode_ui_request(encode(UiRequest(SubmitRequest{kSnapshot, "", "take-" + std::string(64, 'b')})).value());
	expect(spoken.has_value() && std::get<SubmitRequest>(spoken.value()).take_id == "take-" + std::string(64, 'b'),
	       "the take ID round trips");
	for (const char *bad : {"{\"type\":\"status\",\"version\":1,\"extra\":1}\n", "{\"type\":\"status\",\"version\":2}\n",
				"{\"type\":\"submit\",\"version\":1,\"snapshot_id\":\"snap-0123456789abcdef\",\"text\":\"\"}\n",
				"{\"type\":\"delete\",\"version\":1}\n", "{\"type\":\"status\",\"version\":1}"})
		expect(!decode_ui_request(bad).has_value(), std::string("refused: ") + bad);
}

void events()
{
	Reports reports;
	reports.items = {{kReport, "text", "fix_in_pr", 81u, "2026-11-08"}, {kReport, "crash", "received", std::nullopt, "2026-11-07"}};
	reports.update_ready = false;
	for (const ReporterEvent &event : {ReporterEvent(SnapshotTaken{kSnapshot}), ReporterEvent(Submitted{kReport}),
					   ReporterEvent(reports), ReporterEvent(Refused{"submit", "REPORT.RATE_LIMITED"})}) {
		auto frame = encode(event);
		expect(frame.has_value() && decode_reporter_event(frame.value()).has_value(), "an event round trips");
	}
	auto bad = reports;
	bad.items[0].status = "deployed";
	expect(!encode(ReporterEvent(bad)).has_value(), "statuses are closed");
	bad = reports;
	bad.items.resize(21, reports.items[0]);
	expect(!encode(ReporterEvent(bad)).has_value(), "the list is bounded");
	expect(!encode(ReporterEvent(Refused{"status", "REPORT.X"})).has_value(), "status is never refused");
}

void client()
{
	int pair[2];
	::socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, pair);
	auto wrong = ReporterClient::for_connected_peer(pair[0], ::getuid() + 1);
	expect(!wrong.has_value() && wrong.error().code == "REPORT.REPORTER_PEER_REJECTED", "another UID is rejected");
	::close(pair[1]);
	::socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, pair);
	auto connected = ReporterClient::for_connected_peer(pair[0], ::getuid());
	expect(connected.has_value(), "the exact UID is accepted");
	if (!connected.has_value())
		return;
	expect(connected.value()->send(StatusRequest{}).has_value(), "send");
	const auto frame = encode(ReporterEvent(SnapshotTaken{kSnapshot})).value();
	(void)::write(pair[1], frame.data(), frame.size());
	auto polled = connected.value()->poll();
	expect(polled.has_value() && polled.value().size() == 1, "poll");
	(void)::write(pair[1], "{}\n", 3);
	expect(!connected.value()->poll().has_value() && !connected.value()->connected(), "a malformed frame closes it");
	::close(pair[1]);
}
} // namespace

int main()
{
	requests();
	events();
	client();
	if (failures == 0)
		std::cout << "bug_reporter_protocol_test passed\n";
	return failures == 0 ? 0 : 1;
}
