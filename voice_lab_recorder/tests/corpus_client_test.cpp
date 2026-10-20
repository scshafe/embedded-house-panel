// The corpus client against an in-memory intake server that keeps the
// session's bytes as voice-lab's does, and against voice-lab's pinned
// example requests, byte for byte.
#include "golden_take.hpp"
#include "voice_lab_recorder/canonical_json.h"
#include "voice_lab_recorder/corpus_client.h"
#include <iostream>
#include <map>
#include <regex>
#include <unistd.h>
#include <vector>

using namespace voice_lab_recorder;
using bhp::http_runtime::HttpMethod;
using bhp::http_runtime::HttpRequest;
using bhp::http_runtime::HttpResponse;
using Json = nlohmann::json;
namespace fs = std::filesystem;

namespace
{
int failures = 0;

void expect(const bool condition, const std::string &what)
{
	if (!condition) {
		++failures;
		std::cerr << "FAILED: " << what << '\n';
	}
}

std::string header(const HttpRequest &request, const std::string &name)
{
	for (const auto &[key, value] : request.headers)
		if (key == name)
			return value;
	return {};
}

HttpResponse answer(long status, const Json &body, std::string etag = {})
{
	return {status, "application/json", std::move(etag), body.dump()};
}

/** The intake as voice-lab's PROTOCOL.md section 4 describes it, in memory. */
class FakeIntake
{
public:
	std::string token{"access-1"};
	int fail_put_after{-1};
	bool misreport_once{false};
	std::vector<std::string> content_ranges;
	std::map<std::string, std::string> held;
	std::string state{"none"};
	Json declared;
	unsigned puts{0};
	std::vector<std::string> paths;

	bhp::http_runtime::Result<HttpResponse> operator()(const HttpRequest &request)
	{
		const auto path = request.url.substr(std::string("https://pc.example").size());
		paths.push_back(path);
		if (header(request, "Authorization") != "Bearer " + token)
			return bhp::http_runtime::Result<HttpResponse>::success(answer(401, {{"error", "unauthorized"}}));
		return bhp::http_runtime::Result<HttpResponse>::success(route(request, path));
	}

	Json status() const
	{
		Json files = Json::array();
		for (const auto &file : declared["files"]) {
			const std::string name = file["path"];
			const auto have = held.count(name) ? held.at(name).size() : 0;
			files.push_back({{"path", name},
					 {"size_bytes", file["size_bytes"]},
					 {"received_bytes", have},
					 {"complete", have == file["size_bytes"].get<std::size_t>()}});
		}
		return {{"session_id", declared["session_id"]},
			{"manifest_sha256", declared["manifest_sha256"]},
			{"state", state},
			{"files", files},
			{"receipt", nullptr},
			{"refusal", nullptr}};
	}

private:
	HttpResponse route(const HttpRequest &request, const std::string &path)
	{
		static const std::regex put(R"(/api/v1/intake/sessions/(take-[0-9a-f]{64})/files/([A-Za-z.]+))");
		std::smatch match;
		if (request.method == HttpMethod::Post && path == "/api/v1/intake/sessions") {
			const bool fresh = state == "none";
			if (fresh) {
				declared = Json::parse(request.body);
				state = "open";
			}
			return answer(fresh ? 201 : 200, status());
		}
		if (request.method == HttpMethod::Get && path.rfind("/api/v1/intake/sessions/", 0) == 0)
			return answer(200, status());
		if (request.method == HttpMethod::Put && std::regex_match(path, match, put)) {
			if (fail_put_after >= 0 && static_cast<int>(puts) >= fail_put_after)
				return {0, "", "", ""}; // replaced by a transport failure below
			++puts;
			const std::string name = match[2];
			const auto range = header(request, "Content-Range");
			content_ranges.push_back(name + " " + range);
			std::smatch parts;
			std::regex_match(range, parts, std::regex(R"(bytes (\d+)-(\d+)/(\d+))"));
			const auto start = std::stoull(parts[1]);
			auto &have = held[name];
			if (misreport_once) {
				misreport_once = false;
				return answer(409, {{"error", "range_not_contiguous"}, {"received_bytes", have.size()}});
			}
			if (start != have.size())
				return answer(409, {{"error", "range_not_contiguous"}, {"received_bytes", have.size()}});
			std::string chunk(request.file_body->length, '\0');
			::pread(request.file_body->descriptor, chunk.data(), chunk.size(),
				static_cast<off_t>(request.file_body->offset));
			have += chunk;
			const bool done = have.size() == std::stoull(parts[3]);
			return answer(done ? 200 : 202, {{"state", done ? "complete" : "partial"}, {"received_bytes", have.size()}});
		}
		if (request.method == HttpMethod::Post && path.size() > 9 && path.substr(path.size() - 9) == "/finalize") {
			state = "accepted";
			return answer(200, status());
		}
		if (path == "/api/v1/corpus/library?profile=xvf3800-linear-4")
			return header(request, "If-None-Match") == "\"v1\"" ? HttpResponse{304, "", "\"v1\"", ""}
									    : answer(200, {{"profile", "xvf3800-linear-4"}}, "\"v1\"");
		if (path == "/api/v1/corpus/library")
			return answer(403, {{"error", "client_scope"}, {"detail", "no"}, {"received_bytes", nullptr}});
		if (path == "/api/v1/catalog")
			return answer(503, {{"error", "catalog_unavailable"}, {"detail", "rebuilding"}, {"received_bytes", nullptr}});
		return answer(404, {{"error", "not_found"}});
	}
};

struct Fixture {
	fs::path root;
	fs::path package;
	FakeIntake server;
	unsigned invalidations{0};
	/** The token the session hands out; the server's own is `server.token`. */
	std::string client_token{"access-1"};
	std::unique_ptr<CorpusClient> client;

	explicit Fixture(std::uint64_t chunk = 16384)
	{
		std::string pattern = (fs::temp_directory_path() / "bhp-corpus-client-XXXXXX").string();
		root = ::mkdtemp(pattern.data());
		fs::create_directory(root / "outbox");
		const auto take = golden::broker_take(root / "broker", golden::golden_wav());
		auto packaged = write_take_package(take.string(), (root / "outbox").string(), golden::inputs());
		package = packaged.has_value() ? fs::path(packaged.value().directory) : fs::path();
		CorpusClientSettings settings;
		settings.api_url = "https://pc.example";
		settings.chunk_bytes = chunk;
		client = std::make_unique<CorpusClient>(
			settings,
			[this](const HttpRequest &request) {
				auto got = server(request);
				if (got.has_value() && got.value().status == 0)
					return bhp::http_runtime::Result<HttpResponse>::failure(
						{"HTTP.TRANSPORT_FAILED", "reset", true, 56});
				return got;
			},
			AccessTokens{[this]() { return Result<std::string>::success(client_token); },
				     [this]() { ++invalidations; }});
	}
	~Fixture() { fs::remove_all(root); }
};

void session_request_is_the_example()
{
	Fixture fixture;
	auto request = intake_session_request(fixture.package.string());
	const auto text = request.has_value() ? canonical_record(request.value()) : Result<std::string>::failure({});
	expect(text.has_value() &&
		       text.value() == golden::read_bytes(golden::examples_root() / "intake-session-request.json"),
	       "the golden package's session request is the example, byte for byte");
}

void upload_resumes_from_the_server()
{
	Fixture fixture;
	fixture.server.fail_put_after = 2;
	const std::string take_id = fixture.package.filename().string();
	auto declared = fixture.client->declare(fixture.package.string());
	expect(declared.has_value() && declared.value()["state"] == "open", "declared");
	auto sent = fixture.client->send_file(fixture.package.string(), take_id, "original.wav");
	expect(!sent.has_value() && sent.error().retryable, "a dropped connection is retryable");
	expect(fixture.server.held["original.wav"].size() == 32768, "the server kept two chunks");

	fixture.server.fail_put_after = -1;
	fixture.server.misreport_once = true;
	auto uploaded = fixture.client->upload(fixture.package.string());
	expect(uploaded.has_value() && uploaded.value()["state"] == "accepted", "upload finalized: accepted");
	const auto wav = golden::golden_wav();
	expect(fixture.server.held["original.wav"] == wav, "the server holds the WAV byte for byte");
	for (const char *name : {"READY", "continuity.json", "manifest.json", "qc.json"})
		expect(fixture.server.held[name] == golden::read_bytes(fixture.package / name), std::string("sent ") + name);
	bool resumed = false;
	for (const auto &range : fixture.server.content_ranges)
		resumed = resumed || range == "original.wav bytes 32768-49151/49196";
	expect(resumed, "the upload resumed at the server's 32768");
	unsigned zero_starts = 0;
	for (const auto &range : fixture.server.content_ranges)
		zero_starts += range == "original.wav bytes 0-16383/49196";
	expect(zero_starts == 1, "no chunk the server held was sent again");
}

void a_401_refreshes_once()
{
	Fixture fixture;
	fixture.server.token = "access-2";
	auto library = fixture.client->library();
	expect(!library.has_value() && library.error().code == "VOICE_LAB.API_UNAUTHORIZED" && fixture.invalidations == 1,
	       "a token the server keeps refusing: one refresh, then sign in again");
	Fixture refreshed;
	refreshed.server.token = "access-2";
	unsigned calls = 0;
	CorpusClientSettings settings;
	settings.api_url = "https://pc.example";
	CorpusClient rotating(
		settings, [&refreshed](const HttpRequest &request) { return refreshed.server(request); },
		AccessTokens{[&calls]() { return Result<std::string>::success(++calls == 1 ? "stale" : "access-2"); },
			     [&refreshed]() { ++refreshed.invalidations; }});
	auto after_refresh = rotating.library();
	expect(after_refresh.has_value() && refreshed.invalidations == 1 && calls == 2,
	       "a 401 refreshes once and the request succeeds with the new token");
}

void views_and_refusals()
{
	Fixture fixture;
	auto library = fixture.client->library();
	expect(library.has_value() && !library.value().not_modified && library.value().etag == "\"v1\"",
	       "the panel's library view with its ETag");
	auto again = fixture.client->library("\"v1\"");
	expect(again.has_value() && again.value().not_modified, "304 with If-None-Match");
	auto catalog = fixture.client->catalog();
	expect(!catalog.has_value() && catalog.error().code == "VOICE_LAB.API_RETRYABLE" && catalog.error().retryable &&
		       catalog.error().context.at("error") == "catalog_unavailable",
	       "503 catalog_unavailable is retryable");
	auto history = fixture.client->history("reference neutral/01");
	expect(fixture.server.paths.back() ==
		       "/api/v1/corpus/prompts/reference%20neutral%2F01/takes?profile=xvf3800-linear-4",
	       "a prompt id is one encoded segment, with the profile");
	expect(!history.has_value() && history.error().code == "VOICE_LAB.API_REFUSED" && !history.error().retryable,
	       "404 is a refusal, not retried");
}

void decisions()
{
	DecisionRequest request{"req-0123456789abcdef0123456789abcdef", "kept", 0, std::nullopt};
	const Json body = {{"schema_version", "cole-voice-lab.api.take-decision-request/v1"},
			   {"request_id", request.request_id},
			   {"decision", request.decision},
			   {"expected_revision", request.expected_revision},
			   {"reason", nullptr}};
	auto text = canonical_record(body);
	expect(text.has_value() &&
		       text.value() == golden::read_bytes(golden::examples_root() / "take-decision-request.json"),
	       "the decision body is the example, byte for byte");
	auto id = new_request_id();
	expect(id.has_value() && std::regex_match(id.value(), std::regex("req-[0-9a-f]{32}")), "a request id's form");
	auto other = new_request_id();
	expect(other.has_value() && other.value() != id.value(), "request ids differ");
}
} // namespace

int main()
{
	session_request_is_the_example();
	upload_resumes_from_the_server();
	a_401_refreshes_once();
	views_and_refusals();
	decisions();
	if (failures)
		std::cerr << failures << " failure(s)\n";
	return failures ? 1 : 0;
}
