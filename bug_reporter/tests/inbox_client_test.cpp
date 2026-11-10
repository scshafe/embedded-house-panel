// The inbox client against the in-memory inbox: chunks, resume, completion and status.
#include "fake_inbox.hpp"
#include <fstream>
using namespace bug_reporter;
using namespace bug_reporter_test;
namespace
{
const std::string kId = "rpt-00000000000000000000000000000001";

void uploads_and_resumes()
{
	FakeInbox inbox;
	InboxSettings settings{inbox.base, {3000, 30000, 65536}, 1000};
	InboxClient client(settings, std::ref(inbox));
	const auto root = temporary_directory("bhp-inbox-client");
	std::string bytes;
	for (int i = 0; i < 2500; ++i)
		bytes += static_cast<char>('a' + i % 26);
	std::ofstream(root + "/logs.jsonl.gz", std::ios::binary) << bytes;

	inbox.truncate_next_put = 400;
	auto first = client.upload_file(kId, "logs.jsonl.gz", root + "/logs.jsonl.gz");
	expect(!first.has_value() && first.error().code == "REPORT.INBOX_UNREACHABLE" && first.error().retryable,
	       "a lost chunk is a retryable failure");
	auto resumed = client.upload_file(kId, "logs.jsonl.gz", root + "/logs.jsonl.gz");
	expect(resumed.has_value() && inbox.files[kId]["logs.jsonl.gz"] == bytes,
	       "the next attempt resumes from what the inbox holds");
	std::size_t puts = 0;
	for (const auto &call : inbox.calls)
		puts += call.rfind("PUT ", 0) == 0;
	expect(puts == 4, "1000-byte chunks: one cut short, then 1000, 1000 and 100");

	const Json manifest = {{"report_id", kId},
			       {"files", {{{"name", "logs.jsonl.gz"}, {"bytes", bytes.size()}, {"sha256", sha256_hex(bytes)}}}}};
	expect(client.complete(kId, manifest.dump()).has_value() && inbox.completed.count(kId), "complete");
	expect(client.complete(kId, manifest.dump()).has_value(), "complete again changes nothing");
	const Json wrong = {{"report_id", kId}, {"files", {{{"name", "logs.jsonl.gz"}, {"bytes", 1}, {"sha256", "00"}}}}};
	auto incomplete = client.complete(kId, wrong.dump());
	expect(!incomplete.has_value() && incomplete.error().code == "REPORT.INBOX_INCOMPLETE", "a short file is 409");
	expect(!client.upload_file(kId, "../etc/passwd", root + "/logs.jsonl.gz").has_value(), "only bundle files go");
}

void counts_statuses_and_failures()
{
	FakeInbox inbox;
	InboxClient client({inbox.base}, std::ref(inbox));
	const auto fp = sha256_hex("fault");
	const std::string batch(32, 'a');
	expect(client.send_count(fp, batch, 3).has_value() && inbox.counts[fp] == 3, "a count");
	expect(client.send_count(fp, batch, 3).has_value() && inbox.counts[fp] == 3, "a retried batch counts once");
	expect(!client.send_count(fp, "not-a-batch", 1).has_value() && !client.send_count(fp, batch, 10'001).has_value(),
	       "a malformed batch or an out-of-range count is never sent");
	inbox.statuses[kId] = {"fix_in_pr", 81};
	auto statuses = client.statuses({kId, "rpt-00000000000000000000000000000002"});
	expect(statuses.has_value() && statuses.value().size() == 1 && statuses.value()[0].status == "fix_in_pr" &&
		       statuses.value()[0].pr == 81u,
	       "statuses of the reports the inbox knows");
	inbox.busy = 1;
	auto busy = client.send_count(fp, std::string(32, 'b'), 1);
	expect(!busy.has_value() && busy.error().code == "REPORT.INBOX_BUSY" && busy.error().retryable, "503 is retryable");
	inbox.forbidden = 1;
	auto forbidden = client.send_count(fp, std::string(32, 'b'), 1);
	expect(!forbidden.has_value() && forbidden.error().retryable, "403 is about the panel, not the request: retryable");
	inbox.refuse.insert(fp);
	auto refused = client.send_count(fp, std::string(32, 'b'), 1);
	expect(!refused.has_value() && refused.error().code == "REPORT.INBOX_REFUSED" && !refused.error().retryable,
	       "400 is about this request: not retryable");
	inbox.refuse.clear();
	inbox.unreachable = true;
	auto down = client.statuses({kId});
	expect(!down.has_value() && down.error().code == "REPORT.INBOX_UNREACHABLE" && down.error().retryable, "no answer");
	expect(!client.statuses({"rpt-x"}).has_value(), "a malformed ID is never sent");
}
} // namespace

int main()
{
	uploads_and_resumes();
	counts_statuses_and_failures();
	if (failures == 0)
		std::cout << "bug_reporter_inbox_client_test passed\n";
	return failures == 0 ? 0 : 1;
}
