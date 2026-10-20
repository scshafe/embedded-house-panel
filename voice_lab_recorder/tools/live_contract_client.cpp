// Live corpus-client.v1 contract client. The orchestrator
// (scripts/voice_lab/live_contract.py) owns voice-lab's fixture server, its
// throwaway TLS certificate and the state directory; this program drives the
// recorder's sign-in code and the HTTP request API against it and prints
// exactly one JSON report line per scenario. No token is ever printed: the
// report carries checks, counts and error codes only.
// Exit status: 0 when every check passed, 1 when a check failed, 2 on usage
// errors.
#include "bhp/http_runtime/http_request.h"
#include "golden_take.hpp"
#include "voice_lab_recorder/corpus_client.h"
#include "voice_lab_recorder/pocket_id_sign_in.h"
#include "voice_lab_recorder/take_package.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <filesystem>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <thread>

using namespace voice_lab_recorder;
using bhp::http_runtime::HttpMethod;
using bhp::http_runtime::HttpRequest;
using bhp::http_runtime::HttpResponse;
using Json = nlohmann::ordered_json;
/** voice-lab's answers, as the corpus client returns them. */
using Wire = nlohmann::json;

namespace
{
constexpr const char *kLibrary = "/api/v1/corpus/library?profile=xvf3800-linear-4";

struct Options {
	std::string scenario, issuer, api_url, ca_file, state_directory;
};

[[noreturn]] void usage(const std::string &reason)
{
	std::cout << Json{{"ok", false}, {"usage_error", reason}}.dump() << '\n';
	std::exit(2);
}

Options parse(int argc, char **argv)
{
	Options o;
	for (int i = 1; i < argc; ++i) {
		const std::string key = argv[i];
		if (i + 1 >= argc)
			usage("missing value for " + key);
		const std::string value = argv[++i];
		if (key == "--scenario")
			o.scenario = value;
		else if (key == "--issuer")
			o.issuer = value;
		else if (key == "--api-url")
			o.api_url = value;
		else if (key == "--ca-file")
			o.ca_file = value;
		else if (key == "--state-directory")
			o.state_directory = value;
		else
			usage("unknown option " + key);
	}
	if (o.scenario.empty() || o.issuer.empty() || o.api_url.empty() || o.ca_file.empty() ||
	    o.state_directory.empty())
		usage("--scenario, --issuer, --api-url, --ca-file and --state-directory are required");
	return o;
}

std::string read_file(const std::string &path)
{
	std::ifstream input(path, std::ios::binary);
	if (!input)
		usage("cannot read " + path);
	std::ostringstream contents;
	contents << input.rdbuf();
	return contents.str();
}

/** Accumulates named checks and facts; prints one machine-readable line. */
class Report
{
public:
	explicit Report(std::string scenario)
	{
		document_["scenario"] = std::move(scenario);
		document_["evidence"] = "simulation_not_hardware_evidence";
		document_["checks"] = Json::object();
		document_["facts"] = Json::object();
	}
	bool check(const std::string &name, bool passed)
	{
		document_["checks"][name] = passed;
		ok_ = ok_ && passed;
		return passed;
	}
	template <typename Value> void fact(const std::string &name, Value value)
	{
		document_["facts"][name] = std::move(value);
	}
	int finish()
	{
		document_["ok"] = ok_;
		std::cout << document_.dump() << '\n';
		std::cout.flush();
		return ok_ ? 0 : 1;
	}

private:
	Json document_;
	bool ok_{true};
};

/** The fixture's endpoints, the recorder's settings and its token store, for one scenario. */
class Live
{
public:
	Live(const Options &options, std::shared_ptr<bhp::http_runtime::HttpRuntime> runtime)
		: options_(options), runtime_(std::move(runtime)), store_(options.state_directory + "/uploader")
	{
		settings.issuer = options.issuer;
		settings.trusted_ca_pem_for_tests = read_file(options.ca_file);
		transport = [this](const HttpRequest &request) {
			if (request.url == endpoints.token)
				++token_requests;
			return bhp::http_runtime::perform(*runtime_, request, runtime_->cancellation());
		};
	}

	bool discover_endpoints(Report &report)
	{
		auto found = discover(settings, transport);
		if (!report.check("discovery_names_the_issuer_and_device_grant", found.has_value())) {
			report.fact("discovery_error", found.error().code);
			return false;
		}
		endpoints = found.value();
		return true;
	}

	std::unique_ptr<VoiceLabSession> session(std::chrono::seconds margin = std::chrono::seconds(60))
	{
		return std::make_unique<VoiceLabSession>(settings, endpoints, transport, store_,
							 [] { return std::chrono::steady_clock::now(); }, margin);
	}

	bhp::http_runtime::Result<HttpResponse> api_get(const std::string &path, const std::string &token,
							const std::string &etag = {})
	{
		HttpRequest request;
		request.method = HttpMethod::Get;
		request.url = options_.api_url + path;
		request.headers = {{"Accept", "application/json"}};
		if (!token.empty())
			request.headers.emplace_back("Authorization", "Bearer " + token);
		if (!etag.empty())
			request.headers.emplace_back("If-None-Match", etag);
		request.budget = {2000, 30000, 1 << 20};
		request.trusted_ca_pem_for_tests = settings.trusted_ca_pem_for_tests;
		return bhp::http_runtime::perform(*runtime_, request, runtime_->cancellation());
	}

	/** A refresh exchange outside the session, with a token the session has already rotated away. */
	bhp::http_runtime::Result<HttpResponse> raw_refresh(const std::string &refresh_token)
	{
		HttpRequest request;
		request.method = HttpMethod::Post;
		request.url = endpoints.token;
		request.headers = {{"Accept", "application/json"},
				   {"Content-Type", "application/x-www-form-urlencoded"}};
		request.body = "grant_type=refresh_token&refresh_token=" + form_encode(refresh_token) +
			       "&client_id=" + form_encode(settings.client_id);
		request.budget = settings.budget;
		request.trusted_ca_pem_for_tests = settings.trusted_ca_pem_for_tests;
		return bhp::http_runtime::perform(*runtime_, request, runtime_->cancellation());
	}

	std::optional<std::string> stored_token()
	{
		auto loaded = store_.load(settings.issuer, settings.client_id);
		return loaded.has_value() ? loaded.value() : std::nullopt;
	}

	/** The corpus client on this session, with chunks of `chunk_bytes`; `drop_after_puts` simulates a lost link. */
	std::unique_ptr<CorpusClient> corpus(VoiceLabSession &session, std::uint64_t chunk_bytes, int drop_after_puts = -1)
	{
		CorpusClientSettings corpus_settings;
		corpus_settings.api_url = options_.api_url;
		corpus_settings.chunk_bytes = chunk_bytes;
		corpus_settings.trusted_ca_pem_for_tests = settings.trusted_ca_pem_for_tests;
		return std::make_unique<CorpusClient>(
			corpus_settings,
			[this, drop_after_puts](const HttpRequest &request) {
				if (request.method == HttpMethod::Put && drop_after_puts >= 0 &&
				    static_cast<int>(puts++) >= drop_after_puts)
					return bhp::http_runtime::Result<HttpResponse>::failure(
						{"HTTP.TRANSPORT_FAILED", "simulated link loss", true, 56});
				return bhp::http_runtime::perform(*runtime_, request, runtime_->cancellation());
			},
			AccessTokens{[&session] { return session.access_token(); },
				     [&session] { session.invalidate_access_token(); }});
	}

	/** The golden take, packaged once into the state directory's outbox. */
	std::string golden_package(Report &report)
	{
		const auto outbox = std::filesystem::path(options_.state_directory) / "outbox";
		const auto take_id = "take-" + golden::example("take/continuity.json")["wav"]["sha256"].get<std::string>();
		if (!std::filesystem::exists(outbox / take_id)) {
			std::filesystem::create_directories(outbox);
			const auto broker = golden::broker_take(std::filesystem::path(options_.state_directory) / "broker",
								golden::golden_wav());
			auto packaged = write_take_package(broker.string(), outbox.string(), golden::inputs());
			report.check("golden_take_packaged", packaged.has_value());
		}
		return (outbox / take_id).string();
	}

	SignInSettings settings;
	Endpoints endpoints;
	HttpTransport transport;
	unsigned token_requests{0};
	unsigned puts{0};

private:
	Options options_;
	std::shared_ptr<bhp::http_runtime::HttpRuntime> runtime_;
	RefreshTokenStore store_;
};

std::string refusal_code(const HttpResponse &response)
{
	const auto body = Json::parse(response.body, nullptr, false);
	return body.is_object() && body.contains("error") && body["error"].is_string() ? body["error"].get<std::string>()
										       : std::string();
}

/** Device-code sign-in through the fake Pocket ID; the refresh token is saved durably. */
int sign_in(Live &live)
{
	Report report("sign_in");
	if (!live.discover_endpoints(report))
		return report.finish();
	auto started = DeviceCodeSignIn::start(live.settings, live.endpoints, live.transport,
					       [] { return std::chrono::steady_clock::now(); });
	if (!report.check("device_code_issued", started.has_value())) {
		report.fact("device_code_error", started.error().code);
		return report.finish();
	}
	auto &flow = started.value();
	report.check("prompt_has_the_uri_and_code",
		     !flow.prompt().verification_uri.empty() && !flow.prompt().user_code.empty());
	report.fact("interval_seconds", static_cast<long>(flow.prompt().interval.count()));
	unsigned pending = 0;
	std::optional<TokenPair> tokens;
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
	while (!tokens && std::chrono::steady_clock::now() < deadline) {
		const unsigned before = live.token_requests;
		auto polled = flow.poll();
		if (!polled.has_value()) {
			report.fact("poll_error", polled.error().code);
			break;
		}
		if (polled.value().state == DeviceCodeSignIn::State::Approved)
			tokens = polled.value().tokens;
		else if (polled.value().state == DeviceCodeSignIn::State::Expired)
			break;
		else if (live.token_requests > before)
			++pending;
		std::this_thread::sleep_for(std::chrono::milliseconds(200));
	}
	report.fact("pending_answers", pending);
	report.fact("token_requests", live.token_requests);
	report.check("pending_before_approval", pending >= 1);
	if (!report.check("approved_with_tokens", tokens && !tokens->access_token.empty() && !tokens->refresh_token.empty()))
		return report.finish();
	auto session = live.session();
	auto adopted = session->adopt(*tokens);
	report.check("refresh_token_saved", adopted.has_value() && live.stored_token() == tokens->refresh_token);
	return report.finish();
}

/** A new process: restore from disk, refresh once, read the panel's catalog and library views. */
int library(Live &live)
{
	Report report("library");
	if (!live.discover_endpoints(report))
		return report.finish();
	const auto saved = live.stored_token();
	auto session = live.session();
	auto restored = session->restore();
	if (!report.check("restored_signed_in", restored.has_value() && restored.value()))
		return report.finish();
	auto token = session->access_token();
	if (!report.check("access_token_by_refresh", token.has_value() && session->refreshes() == 1)) {
		if (!token.has_value())
			report.fact("refresh_error", token.error().code);
		return report.finish();
	}
	const auto rotated = live.stored_token();
	report.check("rotated_token_saved_before_use", rotated && saved && *rotated != *saved);

	auto catalog = live.api_get("/api/v1/catalog", token.value());
	const bool catalog_ok = catalog.has_value() && catalog.value().status == 200 && !catalog.value().etag.empty();
	report.check("catalog_with_etag", catalog_ok);
	if (catalog_ok) {
		const auto view = Json::parse(catalog.value().body, nullptr, false);
		report.check("catalog_is_json", view.is_object());
		auto unchanged = live.api_get("/api/v1/catalog", token.value(), catalog.value().etag);
		report.check("catalog_not_modified", unchanged.has_value() && unchanged.value().status == 304);
	}

	auto view = live.api_get(kLibrary, token.value());
	const bool view_ok = view.has_value() && view.value().status == 200 && !view.value().etag.empty();
	report.check("profile_library_with_etag", view_ok);
	if (view_ok) {
		const auto body = Json::parse(view.value().body, nullptr, false);
		report.check("profile_library_names_the_profile",
			     body.is_object() && body.value("profile", std::string()) == "xvf3800-linear-4");
		auto unchanged = live.api_get(kLibrary, token.value(), view.value().etag);
		report.check("profile_library_not_modified", unchanged.has_value() && unchanged.value().status == 304);
	}

	auto desktop_view = live.api_get("/api/v1/corpus/library", token.value());
	report.check("desktop_view_refused_client_scope", desktop_view.has_value() &&
								  desktop_view.value().status == 403 &&
								  refusal_code(desktop_view.value()) == "client_scope");
	auto anonymous = live.api_get(kLibrary, {});
	report.check("no_token_refused", anonymous.has_value() && anonymous.value().status == 401);
	if (anonymous.has_value())
		report.fact("no_token_refusal", refusal_code(anonymous.value()));
	return report.finish();
}

/** An error's code and the server's refusal code, if any. */
std::string describe(const Error &error)
{
	const auto refusal = error.context.find("error");
	return error.code + (refusal == error.context.end() ? std::string() : " " + refusal->second);
}

std::optional<Wire> file_entry(const Wire &status, const std::string &path)
{
	if (status.is_object() && status.contains("files"))
		for (const auto &file : status["files"])
			if (file.value("path", std::string()) == path)
				return file;
	return std::nullopt;
}

/** Polls `poll` every 200 ms for up to 30 s while `pending` holds. */
template <typename Poll, typename Pending> Result<Wire> await(Poll poll, Pending pending)
{
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
	for (;;) {
		auto answer = poll();
		if (!answer.has_value() || !pending(answer.value()) || std::chrono::steady_clock::now() > deadline)
			return answer;
		std::this_thread::sleep_for(std::chrono::milliseconds(200));
	}
}

/** Declare the golden take and lose the link after two 16 KiB chunks of its WAV. */
int intake_interrupted(Live &live)
{
	Report report("intake_interrupted");
	if (!live.discover_endpoints(report))
		return report.finish();
	auto session = live.session();
	auto restored = session->restore();
	if (!report.check("restored_signed_in", restored.has_value() && restored.value()))
		return report.finish();
	const auto package = live.golden_package(report);
	const auto take_id = std::filesystem::path(package).filename().string();
	auto client = live.corpus(*session, 16384, 2);
	auto declared = client->declare(package);
	report.check("declared_open", declared.has_value() && declared.value().value("state", std::string()) == "open");
	if (!declared.has_value())
		report.fact("declare_error", describe(declared.error()));
	auto sent = client->send_file(package, take_id, "original.wav");
	report.check("link_loss_is_retryable", !sent.has_value() && sent.error().retryable);
	auto status = client->session(take_id);
	const auto wav = status.has_value() ? file_entry(status.value(), "original.wav") : std::nullopt;
	const auto held = wav ? wav->value("received_bytes", std::uint64_t{0}) : 0;
	report.fact("server_received_bytes", held);
	report.check("server_kept_two_chunks", held == 32768);
	return report.finish();
}

/** A new process: declare again, resume from the server's offset, finalize, and wait for admission. */
int intake_resume(Live &live)
{
	Report report("intake_resume");
	if (!live.discover_endpoints(report))
		return report.finish();
	auto session = live.session();
	auto restored = session->restore();
	if (!report.check("restored_signed_in", restored.has_value() && restored.value()))
		return report.finish();
	const auto package = live.golden_package(report);
	const auto take_id = std::filesystem::path(package).filename().string();
	auto client = live.corpus(*session, 16384);
	auto declared = client->declare(package);
	const auto wav = declared.has_value() ? file_entry(declared.value(), "original.wav") : std::nullopt;
	report.check("redeclared_session_kept_its_bytes",
		     wav && wav->value("received_bytes", std::uint64_t{0}) == 32768);
	auto finalized = client->upload(package);
	if (!report.check("uploaded_and_finalized", finalized.has_value())) {
		report.fact("upload_error", describe(finalized.error()));
		return report.finish();
	}
	auto settled = await([&] { return client->session(take_id); },
			     [](const Wire &status) { return status.value("state", std::string()) == "finalized"; });
	const auto state = settled.has_value() ? settled.value().value("state", std::string()) : std::string();
	report.fact("session_state", state);
	if (settled.has_value() && settled.value().contains("refusal") && settled.value()["refusal"].is_string())
		report.fact("refusal", settled.value()["refusal"].get<std::string>());
	report.check("accepted_with_receipt", state == "accepted" && settled.value().contains("receipt") &&
						      settled.value()["receipt"].is_object() &&
						      settled.value()["receipt"].value("session_id", std::string()) == take_id);
	auto library = client->library();
	bool listed = false;
	if (library.has_value())
		for (const auto &row : library.value().body.value("rows", Wire::array()))
			listed = listed || row.value("latest_take_id", Wire()) == take_id;
	report.check("library_lists_the_take", listed);
	return report.finish();
}

/** Keep the take with a stable request id, replay it, then meet a revision conflict. */
int decide(Live &live)
{
	Report report("decide");
	if (!live.discover_endpoints(report))
		return report.finish();
	auto session = live.session();
	auto restored = session->restore();
	if (!report.check("restored_signed_in", restored.has_value() && restored.value()))
		return report.finish();
	auto client = live.corpus(*session, 16384);
	const Wire manifest = golden::example("take/manifest.json");
	const std::string take_id = manifest["take_id"];
	const std::string prompt_id = manifest["prompt"]["prompt_id"];
	auto revision_of = [&]() -> std::optional<std::uint64_t> {
		auto history = client->history(prompt_id);
		if (history.has_value())
			for (const auto &take : history.value().body.value("takes", Wire::array()))
				if (take.value("take_id", std::string()) == take_id && take["decision_revision"].is_number_unsigned())
					return take["decision_revision"].get<std::uint64_t>();
		return std::nullopt;
	};
	const auto revision = revision_of();
	if (!report.check("history_lists_the_take", revision.has_value()))
		return report.finish();
	report.fact("revision_before", *revision);
	auto id = new_request_id();
	const DecisionRequest keep{id.value(), "kept", *revision, std::nullopt};
	auto queued = client->decide(take_id, keep);
	report.check("decision_queued", queued.has_value());
	auto status = [&](const std::string &request_id) {
		return await([&] { return client->request_status(request_id); },
			     [](const Wire &answer) { return answer.value("status", std::string()) == "queued"; });
	};
	auto applied = status(keep.request_id);
	report.check("decision_applied", applied.has_value() && applied.value().value("status", std::string()) == "applied");
	auto replayed = client->decide(take_id, keep);
	auto replay_status = status(keep.request_id);
	report.check("replay_answers_the_same_request",
		     replayed.has_value() && replay_status.has_value() &&
			     replay_status.value().value("status", std::string()) == "applied" &&
			     replay_status.value()["outcome"] == applied.value()["outcome"]);
	const auto after = revision_of();
	report.check("revision_advanced_once", after && *after == *revision + 1);
	auto stale_id = new_request_id();
	const DecisionRequest stale{stale_id.value(), "rejected", *revision, std::string("too quiet")};
	auto conflict_queued = client->decide(take_id, stale);
	auto conflict = status(stale.request_id);
	const Wire outcome = conflict.has_value() ? conflict.value().value("outcome", Wire()) : Wire();
	report.check("stale_revision_refused",
		     conflict_queued.has_value() && conflict.has_value() &&
			     conflict.value().value("status", std::string()) == "refused" && outcome.is_object() &&
			     outcome.value("code", std::string()) == "revision_conflict");
	auto library = client->library();
	std::string row_status;
	if (library.has_value())
		for (const auto &row : library.value().body.value("rows", Wire::array()))
			if (row.value("prompt_id", std::string()) == prompt_id)
				row_status = row.value("status", std::string());
	report.fact("prompt_status", row_status);
	report.check("prompt_kept_in_the_panel_view", row_status == "kept");
	return report.finish();
}

/** Reusing a rotated refresh token revokes the family; the session then signs out and clears the store. */
int reuse_revokes(Live &live)
{
	Report report("reuse_revokes");
	if (!live.discover_endpoints(report))
		return report.finish();
	const auto before = live.stored_token();
	// A margin longer than the 15-minute access token makes every call refresh.
	auto session = live.session(std::chrono::minutes(16));
	auto restored = session->restore();
	if (!report.check("restored_signed_in", before && restored.has_value() && restored.value()))
		return report.finish();
	auto first = session->access_token();
	report.check("refresh_rotates", first.has_value() && live.stored_token() != before);
	auto reused = live.raw_refresh(*before);
	report.check("reused_token_refused_invalid_grant",
		     reused.has_value() && reused.value().status == 400 && refusal_code(reused.value()) == "invalid_grant");
	auto after = session->access_token();
	report.check("family_revoked_signs_out",
		     !after.has_value() && after.error().code == "VOICE_LAB.SIGN_IN_REQUIRED" && !session->signed_in());
	report.check("store_cleared", !live.stored_token().has_value());
	auto again = session->access_token();
	report.check("signed_out_needs_sign_in", !again.has_value() && again.error().code == "VOICE_LAB.SIGN_IN_REQUIRED");
	return report.finish();
}
} // namespace

int main(int argc, char **argv)
{
	const auto options = parse(argc, argv);
	auto created_runtime = bhp::http_runtime::HttpRuntime::create();
	if (!created_runtime.has_value())
		usage("HTTP runtime unavailable");
	auto runtime = created_runtime.value();
	int status = 2;
	{
		Live live(options, runtime);
		if (options.scenario == "sign_in")
			status = sign_in(live);
		else if (options.scenario == "library")
			status = library(live);
		else if (options.scenario == "intake_interrupted")
			status = intake_interrupted(live);
		else if (options.scenario == "intake_resume")
			status = intake_resume(live);
		else if (options.scenario == "decide")
			status = decide(live);
		else if (options.scenario == "reuse_revokes")
			status = reuse_revokes(live);
		else
			usage("unknown scenario " + options.scenario);
	}
	if (!runtime->shutdown().has_value())
		status = 1;
	return status;
}
