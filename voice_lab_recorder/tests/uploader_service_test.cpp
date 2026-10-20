// The uploader service offline: a fake Pocket ID and API answer discovery,
// refresh, the catalog and the library; the resolver is a fake. The live
// check runs the whole service against voice-lab's own server.
#include "golden_take.hpp"
#include "voice_lab_recorder/uploader_service.h"
#include <iostream>

using namespace voice_lab_recorder;
using bhp::http_runtime::HttpRequest;
using bhp::http_runtime::HttpResponse;
using Json = nlohmann::json;
namespace fs = std::filesystem;
namespace wire = voice_lab_recorder::uploader_protocol;

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

const std::string kIssuer = "https://id.example";

struct World {
	fs::path root;
	bool resolvable{true};
	unsigned refreshes{0};
	unsigned catalog_reads{0};
	std::unique_ptr<UploaderService> service;

	World()
	{
		std::string pattern = (fs::temp_directory_path() / "bhp-uploader-service-XXXXXX").string();
		root = ::mkdtemp(pattern.data());
		for (const char *name : {"token", "handoff", "outbox", "home-host"}) {
			fs::create_directory(root / name);
			fs::permissions(root / name, fs::perms::owner_all, fs::perm_options::replace);
		}
	}
	~World() { fs::remove_all(root); }

	bhp::http_runtime::Result<HttpResponse> answer(const HttpRequest &request)
	{
		auto json = [](long status, const Json &body, std::string etag = {}) {
			return bhp::http_runtime::Result<HttpResponse>::success(
				{status, "application/json", std::move(etag), body.dump()});
		};
		if (request.url == kIssuer + "/.well-known/openid-configuration")
			return json(200, {{"issuer", kIssuer},
					  {"device_authorization_endpoint", kIssuer + "/api/oidc/device/authorize"},
					  {"token_endpoint", kIssuer + "/api/oidc/token"},
					  {"grant_types_supported", {"urn:ietf:params:oauth:grant-type:device_code", "refresh_token"}}});
		if (request.url == kIssuer + "/api/oidc/token") {
			++refreshes;
			return json(200, {{"access_token", "access-" + std::to_string(refreshes)},
					  {"refresh_token", "refresh-" + std::to_string(refreshes)},
					  {"token_type", "Bearer"},
					  {"expires_in", 900}});
		}
		if (request.url == "https://pc.example/api/v1/catalog") {
			++catalog_reads;
			const Json manifest = golden::example("take/manifest.json");
			return json(200, {{"reference", manifest["catalog"]}, {"suite", {{"prompts", Json::array({manifest["prompt"]})}}}},
				    "\"c1\"");
		}
		if (request.url == "https://pc.example/api/v1/corpus/library?profile=xvf3800-linear-4")
			return json(200, {{"rows", Json::array({{{"prompt_id", "reference-neutral-01"}, {"status", "missing"}}})}},
				    "\"l1\"");
		return json(404, {{"error", "not_found"}});
	}

	bool measured{true};

	void open()
	{
		const Json profile = golden::example("take/manifest.json")["capture_profile"];
		UploaderSettings settings;
		settings.sign_in.issuer = kIssuer;
		settings.corpus.api_url = "https://pc.example";
		settings.token_directory = (root / "token").string();
		settings.outbox.handoff_directory = (root / "handoff").string();
		settings.outbox.outbox_directory = (root / "outbox").string();
		settings.outbox.home_host_directory = (root / "home-host").string();
		settings.outbox.channels = measured ? profile["channels"] : Json::array();
		settings.outbox.room = profile["room"];
		settings.outbox.primary_channel = profile["primary_channel"];
		settings.outbox.client = {{"name", "embedded-house-panel"}, {"version", "0"}, {"platform", "Linux aarch64"}};
		settings.outbox.capture_host = "house-panel";
		settings.approval_endpoint = "voice-lab.example.ts.net";
		settings.tailnet_suffix = "example.ts.net";
		UploaderPorts ports;
		ports.transport = [this](const HttpRequest &request) { return answer(request); };
		ports.resolve = [this](const std::string &) {
			return resolvable ? Result<PeerIdentity>::success({"nEXAMPLE11CNTRL", {"tag:voice-lab"},
									    "voice-lab.example.ts.net."})
					  : Result<PeerIdentity>::failure({"VOICE_LAB.TAILNET_UNAVAILABLE", "", {}, true});
		};
		ports.utc_now = [] { return std::string("2026-10-19T12:00:00Z"); };
		ports.steady_now = [] { return std::chrono::steady_clock::now(); };
		ports.random01 = [] { return 0.0; };
		ports.request_id = new_request_id;
		auto opened = UploaderService::open(settings, ports);
		service = opened.has_value() ? std::move(opened.value()) : nullptr;
	}
};

const wire::Refused *refused_of(const std::vector<wire::UploaderEvent> &events)
{
	return events.empty() ? nullptr : std::get_if<wire::Refused>(&events[0]);
}

void signed_out_and_unresolvable()
{
	World world;
	world.resolvable = false;
	world.open();
	if (!world.service)
		return expect(false, "service opens");
	const auto state = world.service->state();
	expect(state.sign_in.state == "signed_out" && state.approval.state == "unavailable" && state.prompts.empty(),
	       "a new uploader is signed out, with nothing to approve");
	expect(world.service->state().sequence == state.sequence, "an unchanged state keeps its sequence");
	world.service->tick();
	expect(world.catalog_reads == 0, "signed out, no catalog is read");
	auto approve = world.service->handle(wire::ApproveRequest{std::string(64, 'a')});
	expect(refused_of(approve) && refused_of(approve)->error_code == "VOICE_LAB.APPROVAL_UNAVAILABLE",
	       "nothing to approve without the PC's node");
	auto keep = world.service->handle(
		wire::KeepRequest{"take-" + std::string(64, 'a'), "reference-neutral-01", "2026-10-19T12:00:00Z"});
	expect(refused_of(keep) && refused_of(keep)->error_code == "VOICE_LAB.PROMPT_NOT_IN_CATALOG",
	       "no catalog, no keep");
}

void restored_session_reads_views_and_approves()
{
	World world;
	RefreshTokenStore((world.root / "token").string()).save(kIssuer, "voice-lab-panel", "refresh-0");
	world.open();
	if (!world.service)
		return expect(false, "service opens");
	auto state = world.service->state();
	expect(state.sign_in.state == "signed_in", "a saved refresh token is signed in");
	expect(state.approval.state == "preview" && state.approval.node_stable_id == "nEXAMPLE11CNTRL" &&
		       state.approval.expires_at == "2027-10-19T12:00:00Z",
	       "a preview for the resolved node, for 365 days");
	const auto before = state.sequence;
	world.service->tick();
	state = world.service->state();
	expect(state.sequence > before && state.catalog_version == 1u && state.prompts.size() == 1 &&
		       state.prompts[0].status == "missing",
	       "the catalog's prompts with the library's status");
	expect(world.refreshes == 1, "the views refreshed the access token once");
	auto wrong = world.service->handle(wire::ApproveRequest{std::string(64, '0')});
	expect(refused_of(wrong) && refused_of(wrong)->error_code == "VOICE_LAB.APPROVAL_DIGEST_MISMATCH",
	       "only the previewed digest approves");
	auto approved = world.service->handle(wire::ApproveRequest{state.approval.approval_sha256});
	const auto *after = approved.empty() ? nullptr : std::get_if<wire::State>(&approved[0]);
	expect(after && after->approval.state == "in_force" &&
		       after->approval.approval_sha256 == state.approval.approval_sha256,
	       "the reviewed approval is in force");
	const auto take = golden::broker_take(world.root / "handoff", golden::golden_wav(), golden::device_record());
	auto kept = world.service->handle(wire::KeepRequest{take.filename().string(), "reference-neutral-01",
							    "2026-10-19T12:00:00Z"});
	const auto *queued = kept.empty() ? nullptr : std::get_if<wire::State>(&kept[0]);
	expect(queued && queued->queued == 1 && fs::exists(world.root / "outbox" / take.filename()),
	       "a kept take is packaged into the outbox");
}

void unmeasured_channel_map()
{
	World world;
	world.measured = false;
	RefreshTokenStore((world.root / "token").string()).save(kIssuer, "voice-lab-panel", "refresh-0");
	world.open();
	if (!world.service)
		return expect(false, "service opens");
	world.service->tick();
	const auto take = golden::broker_take(world.root / "handoff", golden::golden_wav(), golden::device_record());
	auto kept = world.service->handle(wire::KeepRequest{take.filename().string(), "reference-neutral-01",
							    "2026-10-19T12:00:00Z"});
	expect(refused_of(kept) && refused_of(kept)->error_code == "VOICE_LAB.CAPTURE_PROFILE_UNMEASURED" &&
		       fs::exists(take),
	       "without a measured channel map a take is not packaged, and stays handed off");
}

void sign_in_flow()
{
	World world;
	world.open();
	if (!world.service)
		return expect(false, "service opens");
	auto started = world.service->handle(wire::SignInRequest{});
	// The fake's device endpoint answers 404: the sign-in is refused, not pending.
	expect(refused_of(started) && refused_of(started)->request == "sign_in", "a refused device code is reported");
	expect(world.service->state().sign_in.state == "signed_out", "and the uploader stays signed out");
}
} // namespace

int main()
{
	signed_out_and_unresolvable();
	restored_session_reads_views_and_approves();
	sign_in_flow();
	unmeasured_channel_map();
	if (failures)
		std::cerr << failures << " failure(s)\n";
	return failures ? 1 : 0;
}
