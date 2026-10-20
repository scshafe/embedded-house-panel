// The uploader's outbox against an in-memory voice-lab (sessions, publication,
// history and decision requests as PROTOCOL.md describes them), a fake node
// resolver and the pinned examples.
#include "golden_take.hpp"
#include "voice_lab_recorder/canonical_json.h"
#include "voice_lab_recorder/outbox.h"
#include <iostream>
#include <map>
#include <regex>
#include <unistd.h>

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

HttpResponse answer(long status, const Json &body)
{
	return {status, "application/json", "\"etag\"", body.dump()};
}

/** voice-lab as a corpus client sees it, in memory. */
class FakeVoiceLab
{
public:
	struct Session {
		Json declared;
		std::map<std::string, std::string> held;
		std::string state{"open"};
	};
	std::map<std::string, Session> sessions;
	std::map<std::string, Json> requests;
	std::map<std::string, std::pair<std::string, std::uint64_t>> takes; // take -> decision, revision
	int drop_puts_after{-1};
	unsigned puts{0};
	unsigned bytes_received{0};
	bool refuse_take{false};
	bool drop_next_decision{false};
	unsigned decisions_posted{0};

	bhp::http_runtime::Result<HttpResponse> operator()(const HttpRequest &request)
	{
		const std::string path = request.url.substr(std::string("https://pc.example").size());
		std::smatch match;
		static const std::regex session_path(R"(/api/v1/intake/sessions/(take-[0-9a-f]{64}))");
		static const std::regex file_path(R"(/api/v1/intake/sessions/(take-[0-9a-f]{64})/files/([A-Za-z.]+))");
		static const std::regex finalize_path(R"(/api/v1/intake/sessions/(take-[0-9a-f]{64})/finalize)");
		static const std::regex history_path(R"(/api/v1/corpus/prompts/([a-z0-9-]+)/takes\?profile=xvf3800-linear-4)");
		static const std::regex decision_path(R"(/api/v1/corpus/takes/(take-[0-9a-f]{64})/decision)");
		static const std::regex request_path(R"(/api/v1/corpus/requests/(req-[0-9a-f]{32}))");
		using R = bhp::http_runtime::Result<HttpResponse>;
		if (request.method == HttpMethod::Post && path == "/api/v1/intake/sessions") {
			const Json body = Json::parse(request.body);
			auto &session = sessions[body["session_id"]];
			const bool fresh = session.declared.is_null();
			if (fresh)
				session.declared = body;
			return R::success(answer(fresh ? 201 : 200, status(body["session_id"])));
		}
		if (request.method == HttpMethod::Put && std::regex_match(path, match, file_path)) {
			if (drop_puts_after >= 0 && static_cast<int>(puts) >= drop_puts_after)
				return R::failure({"HTTP.TRANSPORT_FAILED", "reset", true, 56});
			++puts;
			auto &have = sessions[match[1]].held[match[2]];
			std::string chunk(request.file_body->length, '\0');
			::pread(request.file_body->descriptor, chunk.data(), chunk.size(),
				static_cast<off_t>(request.file_body->offset));
			if (request.file_body->offset != have.size())
				return R::success(answer(409, {{"error", "range_not_contiguous"}, {"received_bytes", have.size()}}));
			have += chunk;
			bytes_received += static_cast<unsigned>(chunk.size());
			return R::success(answer(202, {{"state", "partial"}, {"received_bytes", have.size()}}));
		}
		if (request.method == HttpMethod::Post && std::regex_match(path, match, finalize_path)) {
			auto &session = sessions[match[1]];
			session.state = refuse_take ? "refused" : "accepted";
			if (!refuse_take)
				takes[match[1]] = {"draft", 0};
			return R::success(answer(200, status(match[1])));
		}
		if (request.method == HttpMethod::Get && std::regex_match(path, match, session_path))
			return R::success(answer(200, status(match[1])));
		if (std::regex_match(path, match, history_path)) {
			Json list = Json::array();
			for (const auto &[take, decision] : takes)
				list.push_back({{"take_id", take}, {"decision", decision.first}, {"decision_revision", decision.second}});
			return R::success(answer(200, {{"takes", list}}));
		}
		if (request.method == HttpMethod::Post && std::regex_match(path, match, decision_path)) {
			if (drop_next_decision) {
				drop_next_decision = false;
				return R::failure({"HTTP.TRANSPORT_FAILED", "reset", true, 56});
			}
			++decisions_posted;
			const Json body = Json::parse(request.body);
			const std::string id = body["request_id"];
			if (!requests.count(id)) {
				auto &take = takes[match[1]];
				const bool current = body["expected_revision"] == take.second;
				Json outcome = {{"code", current ? Json(nullptr) : Json("revision_conflict")}};
				if (current)
					take = {body["decision"], take.second + 1};
				requests[id] = {{"request_id", id}, {"status", current ? "applied" : "refused"}, {"outcome", outcome}};
			}
			Json queued = requests[id];
			queued["status"] = "queued";
			return R::success(answer(202, queued));
		}
		if (std::regex_match(path, match, request_path)) {
			if (!requests.count(match[1]))
				return R::success(answer(404, {{"error", "request_not_found"}}));
			return R::success(answer(200, requests[match[1]]));
		}
		return R::success(answer(404, {{"error", "not_found"}}));
	}

	Json status(const std::string &take_id)
	{
		auto &session = sessions[take_id];
		Json files = Json::array();
		for (const auto &file : session.declared["files"]) {
			const std::string name = file["path"];
			const auto have = session.held[name].size();
			files.push_back({{"path", name},
					 {"size_bytes", file["size_bytes"]},
					 {"received_bytes", have},
					 {"complete", have == file["size_bytes"].get<std::size_t>()}});
		}
		Json receipt = nullptr;
		if (session.state == "accepted")
			receipt = {{"schema_version", "cole-voice-lab.intake-receipt/v1"},
				   {"session_id", take_id},
				   {"manifest_sha256", session.declared["manifest_sha256"]},
				   {"source_qc_report_sha256", std::string(64, 'b')},
				   {"intake_qc_report_sha256", std::string(64, 'c')},
				   {"accepted_at", "2026-10-19T12:00:10Z"},
				   {"raw_path", "raw/corpus/" + take_id}};
		return {{"session_id", take_id},
			{"manifest_sha256", session.declared["manifest_sha256"]},
			{"state", session.state},
			{"files", files},
			{"receipt", receipt},
			{"refusal", session.state == "refused" ? Json("take_catalog_invalid: no such version") : Json(nullptr)}};
	}
};

struct World {
	fs::path root;
	FakeVoiceLab voice_lab;
	bool signed_in{true};
	PeerIdentity peer{"nEXAMPLE11CNTRL", {"tag:voice-lab"}, "voice-lab.example.ts.net."};
	std::chrono::steady_clock::time_point now{std::chrono::steady_clock::now()};
	unsigned ids{0};
	std::unique_ptr<CorpusClient> client;

	World()
	{
		std::string pattern = (fs::temp_directory_path() / "bhp-outbox-XXXXXX").string();
		root = ::mkdtemp(pattern.data());
		for (const char *name : {"handoff", "outbox", "home-host"}) {
			fs::create_directory(root / name);
			fs::permissions(root / name, fs::perms::owner_all, fs::perm_options::replace);
		}
		CorpusClientSettings settings;
		settings.api_url = "https://pc.example";
		settings.chunk_bytes = 16384;
		client = std::make_unique<CorpusClient>(
			settings, [this](const HttpRequest &request) { return voice_lab(request); },
			AccessTokens{[this]() {
					     return signed_in ? Result<std::string>::success("access")
							      : Result<std::string>::failure(
									{"VOICE_LAB.SIGN_IN_REQUIRED", "sign in", {}, false});
				     },
				     [] {}});
	}
	~World() { fs::remove_all(root); }

	void approve()
	{
		HomeHost home((root / "home-host").string());
		const Json approval = golden::example("home-host-approval.json");
		(void)home.approve(approval, record_sha256(approval).value(), "2026-10-18T12:01:00Z");
	}

	std::unique_ptr<Outbox> open()
	{
		const Json profile = golden::example("take/manifest.json")["capture_profile"];
		OutboxSettings settings;
		settings.handoff_directory = (root / "handoff").string();
		settings.outbox_directory = (root / "outbox").string();
		settings.home_host_directory = (root / "home-host").string();
		settings.channels = profile["channels"];
		settings.room = profile["room"];
		settings.primary_channel = profile["primary_channel"];
		settings.client = golden::example("take/manifest.json")["client"];
		settings.capture_host = "house-panel";
		settings.backoff = {std::chrono::milliseconds(1000), std::chrono::milliseconds(8000), 0.0};
		OutboxPorts ports;
		ports.resolve = [this](const std::string &) { return Result<PeerIdentity>::success(peer); };
		ports.utc_now = [] { return std::string("2026-10-19T12:00:06Z"); };
		ports.steady_now = [this] { return now; };
		ports.random01 = [] { return 0.0; };
		ports.request_id = [this] {
			char id[40];
			std::snprintf(id, sizeof id, "req-%032u", ++ids);
			return Result<std::string>::success(id);
		};
		auto outbox = Outbox::open(settings, *client, ports);
		return outbox.has_value() ? std::move(outbox.value()) : nullptr;
	}

	KeepRequest hand_off(const std::string &wav)
	{
		const auto take = golden::broker_take(root / "handoff", wav, golden::device_record());
		const Json manifest = golden::example("take/manifest.json");
		return {take.filename().string(), manifest["prompt"], manifest["catalog"], manifest["requested_at"]};
	}

	/** Steps until idle, blocked or `limit` steps, letting the clock pass any wait. */
	OutboxStep run(Outbox &outbox, int limit = 40)
	{
		OutboxStep last;
		for (int at = 0; at < limit; ++at) {
			last = outbox.step();
			if (last.kind == OutboxStep::Kind::Idle || last.kind == OutboxStep::Kind::Blocked)
				return last;
			now += last.retry_after;
		}
		return last;
	}

	std::vector<Json> ledger()
	{
		std::vector<Json> events;
		auto entries = HomeHost((root / "home-host").string()).entries();
		if (entries.has_value())
			for (const auto &[entry, digest] : entries.value())
				events.push_back(entry);
		return events;
	}
};

void blocked_until_approved_and_the_node_matches()
{
	World world;
	auto outbox = world.open();
	auto kept = outbox->keep(world.hand_off(golden::golden_wav()));
	expect(kept.has_value() && kept.value()["stage"] == "queued", "a handed-off take is queued");
	expect(!fs::exists(world.root / "handoff" / kept.value()["take_id"].get<std::string>()),
	       "the hand-off copy is removed once the package is durable");
	auto step = outbox->step();
	expect(step.kind == OutboxStep::Kind::Blocked && step.reason == "VOICE_LAB.APPROVAL_MISSING",
	       "nothing is sent without an approval");
	world.approve();
	world.peer.node_stable_id = "nOTHER";
	step = outbox->step();
	expect(step.kind == OutboxStep::Kind::Blocked && step.reason == "VOICE_LAB.HOME_HOST_IDENTITY_MISMATCH",
	       "nothing is sent to another node");
	expect(world.voice_lab.sessions.empty() && world.ledger().empty(), "no session and no ledger entry yet");
	expect(outbox->status()["blocked"] == "VOICE_LAB.HOME_HOST_IDENTITY_MISMATCH", "the status says why");
	world.signed_in = false;
	world.peer.node_stable_id = "nEXAMPLE11CNTRL";
	step = outbox->step();
	expect(step.kind == OutboxStep::Kind::Blocked && step.reason == "VOICE_LAB.SIGN_IN_REQUIRED",
	       "signed out blocks the queue");
}

void a_take_goes_to_kept_with_its_ledger()
{
	World world;
	world.approve();
	auto outbox = world.open();
	const auto request = world.hand_off(golden::golden_wav());
	(void)outbox->keep(request);
	auto again = outbox->keep(request);
	expect(again.has_value() && outbox->status()["takes"].size() == 1, "keeping a take twice queues it once");
	const auto last = world.run(*outbox);
	expect(last.kind == OutboxStep::Kind::Idle, "the queue drains");
	expect(outbox->status()["takes"][0]["stage"] == "kept", "the take is kept");
	expect(world.voice_lab.takes[request.take_id].first == "kept", "voice-lab holds it as kept");
	const auto ledger = world.ledger();
	expect(ledger.size() == 2 && ledger[0]["event"] == "upload-started" && ledger[1]["event"] == "upload-completed",
	       "upload-started, then upload-completed");
	const Json receipt = world.voice_lab.status(request.take_id)["receipt"];
	expect(ledger.size() == 2 && ledger[1]["receipt_sha256"] == record_sha256(receipt).value(),
	       "upload-completed names the receipt's digest");
	const auto package = world.root / "outbox" / request.take_id;
	const Json manifest = Json::parse(golden::read_bytes(package / "manifest.json"));
	expect(golden::read_bytes(package / "manifest.json") == golden::read_bytes(golden::examples_root() / "take/manifest.json"),
	       "the device record and board settings give the golden manifest");
	expect(manifest["take_id"] == request.take_id, "the package is the take");
}

void a_restart_resumes_from_the_server()
{
	World world;
	world.approve();
	const auto request = world.hand_off(golden::golden_wav());
	{
		auto outbox = world.open();
		(void)outbox->keep(request);
		world.voice_lab.drop_puts_after = 2;
		const auto step = outbox->step();
		expect(step.kind == OutboxStep::Kind::Waiting && step.retry_after.count() == 1000,
		       "a dropped link waits the first backoff");
	}
	world.voice_lab.drop_puts_after = -1;
	auto reopened = world.open();
	expect(reopened && reopened->status()["takes"][0]["stage"] == "queued", "the queue survives a restart");
	world.run(*reopened);
	expect(reopened->status()["takes"][0]["stage"] == "kept", "and finishes");
	expect(world.voice_lab.bytes_received == golden::golden_wav().size() + 398 + 631 + 2676 + 565,
	       "no byte the server held was sent again");
	expect(world.ledger().size() == 3, "each run's attempt writes upload-started once");
}

void a_refused_take_ends()
{
	World world;
	world.approve();
	world.voice_lab.refuse_take = true;
	auto outbox = world.open();
	(void)outbox->keep(world.hand_off(golden::variant_wav(5)));
	world.run(*outbox);
	const Json take = outbox->status()["takes"][0];
	expect(take["stage"] == "refused" && take["error"].get<std::string>().rfind("take_catalog_invalid", 0) == 0,
	       "a refused session ends the take with voice-lab's reason");
	const auto ledger = world.ledger();
	expect(ledger.size() == 2 && ledger[1]["event"] == "upload-refused", "upload-refused in the ledger");
}

void a_lost_decision_is_sent_again_with_its_id()
{
	World world;
	world.approve();
	world.voice_lab.drop_next_decision = true;
	auto outbox = world.open();
	const auto request = world.hand_off(golden::golden_wav());
	(void)outbox->keep(request);
	world.run(*outbox);
	expect(outbox->status()["takes"][0]["stage"] == "kept", "kept after the lost decision");
	expect(world.voice_lab.requests.size() == 1 && world.voice_lab.requests.count("req-00000000000000000000000000000001"),
	       "one request, under the id saved before the first send");
}

void the_outbox_has_a_limit()
{
	World world;
	auto outbox = world.open();
	auto missing = outbox->keep({"take-" + std::string(64, 'e'), {}, {}, "2026-10-19T12:00:00Z"});
	expect(!missing.has_value() && missing.error().code == "VOICE_LAB.HANDOFF_MISSING",
	       "a take the broker did not hand over is refused");
	const auto take = golden::broker_take(world.root / "handoff", golden::golden_wav());
	const Json manifest = golden::example("take/manifest.json");
	auto unread = outbox->keep({take.filename().string(), manifest["prompt"], manifest["catalog"], "2026-10-19T12:00:00Z"});
	expect(!unread.has_value() && unread.error().code == "VOICE_LAB.DEVICE_UNREAD",
	       "a take without its device record is refused, not filled in");
}
} // namespace

int main()
{
	blocked_until_approved_and_the_node_matches();
	a_take_goes_to_kept_with_its_ledger();
	a_restart_resumes_from_the_server();
	a_refused_take_ends();
	a_lost_decision_is_sent_again_with_its_id();
	the_outbox_has_a_limit();
	if (failures)
		std::cerr << failures << " failure(s)\n";
	return failures ? 1 : 0;
}
