// The bhp.bug-report/v1 bundle: its files, its bounds and its draft rules.
#include "fake_inbox.hpp"
#include <fstream>
#include <iterator>
#include <sys/stat.h>
#include <zlib.h>
using namespace bug_reporter;
using namespace bug_reporter_test;
namespace
{
const std::string kId = "rpt-0123456789abcdef0123456789abcdef";

std::string read_file(const std::string &path)
{
	std::ifstream input(path, std::ios::binary);
	return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::string gunzip(const std::string &bytes)
{
	z_stream stream{};
	inflateInit2(&stream, 15 + 16);
	std::string out(64 * 1024 * 1024, '\0');
	stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(bytes.data()));
	stream.avail_in = static_cast<uInt>(bytes.size());
	stream.next_out = reinterpret_cast<Bytef *>(out.data());
	stream.avail_out = static_cast<uInt>(out.size());
	inflate(&stream, Z_FINISH);
	out.resize(stream.total_out);
	inflateEnd(&stream);
	return out;
}

ReportDraft text_draft()
{
	ReportDraft draft;
	draft.kind = "text";
	draft.component = "panel_ui";
	draft.screen = {"music", "playing", "day_active", "present", "day", "active"};
	draft.description_text = "Skip did nothing.\nTwice.";
	draft.log_lines = {R"({"level":"info","trace_id":"t-1"})", "plain text from a library",
			   R"({"level":"warn","trace_id":"t-2"})", R"({"level":"info","trace_id":"t-1"})"};
	return draft;
}

void writes_a_bundle()
{
	const auto root = temporary_directory("bhp-bundle");
	auto bundle = write_bundle(root + "/" + kId, kId, "2026-11-08T12:00:00Z", text_draft(),
				   {{"board_id", "frdm-imx8mp"}, {"commit", "abc123"}});
	expect(bundle.has_value(), "a text report writes");
	if (!bundle.has_value())
		return;
	const auto &manifest = bundle.value().manifest;
	expect(manifest["schema"] == "bhp.bug-report/v1" && manifest["kind"] == "text" && manifest["error_code"].is_null() &&
		       manifest["screen"]["surface"] == "music" && manifest["build"]["commit"] == "abc123",
	       "the manifest's fields");
	expect(manifest["trace_ids"] == Json::array({"t-2", "t-1"}), "the newest distinct trace IDs");
	expect(manifest["description_text"] == "Skip did nothing.\nTwice.", "the typed text is in the private manifest");
	expect(gunzip(read_file(root + "/" + kId + "/logs.jsonl.gz")) ==
		       "{\"level\":\"info\",\"trace_id\":\"t-1\"}\nplain text from a library\n"
		       "{\"level\":\"warn\",\"trace_id\":\"t-2\"}\n{\"level\":\"info\",\"trace_id\":\"t-1\"}\n",
	       "the logs gunzip to the lines");
	const auto files = bundle.value().files;
	expect(files.size() == 1 && files[0].name == "logs.jsonl.gz" &&
		       files[0].sha256 == sha256_hex(read_file(root + "/" + kId + "/logs.jsonl.gz")),
	       "each file is listed with its hash");
	struct stat folder {};
	::stat((root + "/" + kId).c_str(), &folder);
	expect((folder.st_mode & 0777) == 0700, "the bundle directory is 0700");
	expect(!write_bundle(root + "/" + kId, kId, "2026-11-08T12:00:00Z", text_draft(), {}).has_value(),
	       "an existing bundle is never overwritten");
}

void bounds_and_rules()
{
	const auto root = temporary_directory("bhp-bundle-rules");
	auto draft = text_draft();
	draft.log_lines.clear();
	// Random-looking lines barely compress, so the bound must cut the oldest ones.
	for (int i = 0; i < 4000; ++i)
		draft.log_lines.push_back("{\"n\":" + std::to_string(i) + ",\"x\":\"" +
					  sha256_hex(std::to_string(i)) + sha256_hex(std::to_string(-i)) + "\"}");
	BundleLimits limits;
	limits.maximum_log_gzip_bytes = 64 * 1024;
	auto bundle = write_bundle(root + "/" + kId, kId, "2026-11-08T12:00:00Z", draft, {}, limits);
	expect(bundle.has_value(), "a large log still writes");
	if (bundle.has_value()) {
		const auto gz = read_file(root + "/" + kId + "/logs.jsonl.gz");
		const auto logs = gunzip(gz);
		expect(gz.size() <= limits.maximum_log_gzip_bytes, "the gzip fits the bound");
		expect(logs.find("\"n\":3999,") != std::string::npos && logs.find("\"n\":0,") == std::string::npos,
		       "the newest lines are kept");
	}
	const auto refused = [&](ReportDraft bad, const char *field) {
		static int n = 0;
		const auto id = "rpt-" + std::string(31, '0') + std::to_string(++n % 10);
		auto result = write_bundle(root + "/" + id, id, "2026-11-08T12:00:00Z", bad, {});
		expect(!result.has_value() && result.error().code == "REPORT.DRAFT_INVALID" &&
			       result.error().context.at("field") == field,
		       std::string("refused: ") + field);
	};
	auto bad = text_draft();
	bad.kind = "chat";
	refused(bad, "kind");
	bad = text_draft();
	bad.component = "Panel UI";
	refused(bad, "component");
	bad = text_draft();
	bad.error_code = "MUSIC.X";
	refused(bad, "error_code");
	bad = text_draft();
	bad.kind = "fault";
	bad.error_code = "lowercase";
	bad.description_text.clear();
	refused(bad, "error_code");
	bad = text_draft();
	bad.description_text = std::string(501, 'x');
	refused(bad, "description_text");
	bad = text_draft();
	bad.description_text = "tab\there";
	refused(bad, "description_text");
	bad = text_draft();
	bad.screen.surface = "Music Screen";
	refused(bad, "screen");
}

void fingerprints_and_build()
{
	ReportDraft fault;
	fault.kind = "fault";
	fault.component = "bhp-music-control";
	fault.error_code = "MUSIC.INTERNAL";
	fault.location = "music_service.cpp:handle";
	expect(fingerprint(fault, "rpt-a") == fingerprint(fault, "rpt-b"), "an automatic fingerprint ignores the ID");
	auto other = fault;
	other.location = "music_service.cpp:tick";
	expect(fingerprint(fault, "rpt-a") != fingerprint(other, "rpt-a"), "the location is part of it");
	expect(fingerprint(text_draft(), "rpt-a") != fingerprint(text_draft(), "rpt-b"), "manual reports never merge");
	const auto root = temporary_directory("bhp-build");
	std::ofstream(root + "/image-version") << "board_id=frdm-imx8mp\ncommit=abc123\nnot a pair\nBAD=x\n";
	const auto build = read_build_info(root + "/image-version");
	expect(build.size() == 2 && build.at("commit") == "abc123", "the image version's pairs");
	expect(read_build_info(root + "/missing").at("image_version") == "unavailable", "no version file");
}
} // namespace

int main()
{
	writes_a_bundle();
	bounds_and_rules();
	fingerprints_and_build();
	if (failures == 0)
		std::cout << "bug_reporter_report_bundle_test passed\n";
	return failures == 0 ? 0 : 1;
}
