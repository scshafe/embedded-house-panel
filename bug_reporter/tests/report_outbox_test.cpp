// The outbox: rate limits, eviction, receipt and status, and a restart in between.
#include "fake_inbox.hpp"
#include "bug_reporter/report_outbox.h"
#include <sys/stat.h>
using namespace bug_reporter;
using namespace bug_reporter_test;
namespace
{
constexpr std::int64_t kNow = 1'794'000'000;

std::string id(int n)
{
	char text[40];
	std::snprintf(text, sizeof text, "rpt-%032x", n);
	return text;
}

Bundle bundle_of(ReportOutbox &outbox, int n, const std::string &kind, std::size_t bytes)
{
	const auto report_id = id(n);
	::mkdir(outbox.bundle_directory(report_id).c_str(), 0700);
	Bundle bundle;
	bundle.report_id = report_id;
	bundle.fingerprint = sha256_hex(kind + std::to_string(n));
	bundle.bytes = bytes;
	return bundle;
}

Admission admit(ReportOutbox &outbox, const std::string &kind, const std::string &fp, std::int64_t at)
{
	auto result = outbox.admit(kind, fp, at);
	return result.has_value() ? result.value().admission : Admission::Refused;
}

void automatic_limits()
{
	auto outbox = std::move(ReportOutbox::open(temporary_directory("bhp-outbox-limits")).value());
	const auto fp = sha256_hex("fault");
	expect(admit(*outbox, "fault", fp, kNow) == Admission::Full, "the first of a fingerprint is a full bundle");
	expect(admit(*outbox, "fault", fp, kNow + 60) == Admission::CountOnly, "a repeat within 24 h is a count");
	expect(admit(*outbox, "fault", fp, kNow + 25 * 3600) == Admission::Full, "after 24 h it is full again");
	expect(admit(*outbox, "fault", fp, kNow + 50 * 3600) == Admission::Full, "the third in a week");
	expect(admit(*outbox, "fault", fp, kNow + 75 * 3600) == Admission::CountOnly, "a fourth in a week is a count");
	expect(outbox->pending_counts().size() == 1 && outbox->pending_counts()[0].second == 2, "two repeats wait");
	expect(outbox->clear_count(fp, 2).has_value() && outbox->pending_counts().empty(), "sent counts clear");

	auto busy = std::move(ReportOutbox::open(temporary_directory("bhp-outbox-busy")).value());
	for (int i = 0; i < 3; ++i)
		expect(admit(*busy, "crash", sha256_hex("c" + std::to_string(i)), kNow + i) == Admission::Full, "three an hour");
	expect(admit(*busy, "crash", sha256_hex("c3"), kNow + 10) == Admission::CountOnly, "a fourth in the hour is a count");
	for (int i = 4; i < 11; ++i)
		(void)admit(*busy, "crash", sha256_hex("c" + std::to_string(i)), kNow + 3600 * (i - 2));
	expect(admit(*busy, "crash", sha256_hex("c99"), kNow + 3600 * 10) == Admission::CountOnly, "ten a day");
}

void manual_limits()
{
	auto outbox = std::move(ReportOutbox::open(temporary_directory("bhp-outbox-manual")).value());
	expect(admit(*outbox, "text", sha256_hex("m1"), kNow) == Admission::Full, "a manual report");
	auto spaced = outbox->admit("text", sha256_hex("m2"), kNow + 30);
	expect(spaced.value().admission == Admission::Refused && spaced.value().refusal_code == "REPORT.RATE_LIMITED",
	       "one a minute");
	for (int i = 1; i < 20; ++i)
		(void)admit(*outbox, "voice", sha256_hex("m" + std::to_string(i + 2)), kNow + 61 * i);
	auto daily = outbox->admit("text", sha256_hex("m99"), kNow + 61 * 21);
	expect(daily.value().admission == Admission::Refused && daily.value().refusal_code == "REPORT.DAILY_LIMIT",
	       "twenty a day");
}

void eviction_and_status()
{
	const auto directory = temporary_directory("bhp-outbox-evict");
	OutboxLimits limits;
	limits.maximum_bytes = 1000;
	limits.maximum_reports = 3;
	limits.status_history = 2;
	{
		auto outbox = std::move(ReportOutbox::open(directory, limits).value());
		expect(outbox->add(bundle_of(*outbox, 1, "fault", 400), "fault", kNow).has_value(), "add a fault");
		expect(outbox->add(bundle_of(*outbox, 2, "text", 400), "text", kNow + 1).has_value(), "add a text report");
		expect(outbox->add(bundle_of(*outbox, 3, "crash", 400), "crash", kNow + 2).has_value(),
		       "the oldest automatic bundle makes room");
		struct stat gone {};
		expect(::stat(outbox->bundle_directory(id(1)).c_str(), &gone) != 0, "its directory is deleted");
		const auto queued = outbox->queued();
		expect(queued.size() == 2 && queued[0].report_id == id(2) && queued[1].report_id == id(3),
		       "the manual report stays");
		expect(outbox->add(bundle_of(*outbox, 4, "text", 400), "text", kNow + 3).has_value(),
		       "the crash makes room for a manual report");
		auto full = outbox->add(bundle_of(*outbox, 5, "text", 400), "text", kNow + 4);
		expect(!full.has_value() && full.error().code == "REPORT.OUTBOX_FULL", "a manual report that cannot fit is refused");
		expect(::stat(outbox->bundle_directory(id(5)).c_str(), &gone) != 0, "and its bundle is removed");
		expect(outbox->mark_received(id(2)).has_value(), "received");
		expect(::stat(outbox->bundle_directory(id(2)).c_str(), &gone) != 0, "a received bundle is deleted");
		expect(outbox->set_status(id(2), "fix_in_pr", 81).has_value(), "a status from the inbox");
		expect(!outbox->set_status(id(2), "deployed", std::nullopt).has_value(), "an unknown status is refused");
	}
	auto reopened = std::move(ReportOutbox::open(directory, limits).value());
	const auto recent = reopened->recent(10);
	expect(recent.size() == 2 && recent[0].report_id == id(4) && recent[1].report_id == id(2) &&
		       recent[1].status == "fix_in_pr" && recent[1].pr == 81u && recent[1].state == "received",
	       "a restart keeps the queue and the statuses");
	expect(reopened->queued().size() == 1, "one still queued");
}
} // namespace

int main()
{
	automatic_limits();
	manual_limits();
	eviction_and_status();
	if (failures == 0)
		std::cout << "bug_reporter_report_outbox_test passed\n";
	return failures == 0 ? 0 : 1;
}
