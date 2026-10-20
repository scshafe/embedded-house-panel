#include "voice_lab_recorder/uploader_service.h"
#include <algorithm>
#include <map>
#include <ctime>

namespace voice_lab_recorder
{
namespace
{
using Json = nlohmann::json;
namespace wire = uploader_protocol;
constexpr std::chrono::minutes kPreviewLifetime{10};
constexpr std::chrono::milliseconds kIdlePoll{5000};
constexpr std::size_t kMaximumPromptText = 2000;

std::string format_utc(std::int64_t seconds)
{
	const auto when = static_cast<std::time_t>(seconds);
	std::tm parts{};
	gmtime_r(&when, &parts);
	char text[32];
	std::strftime(text, sizeof text, "%Y-%m-%dT%H:%M:%SZ", &parts);
	return text;
}

/** At most `limit` bytes, cut on a UTF-8 character boundary. */
std::string clip(const std::string &text, std::size_t limit)
{
	if (text.size() <= limit)
		return text;
	std::size_t end = limit;
	while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80)
		--end;
	return text.substr(0, end);
}

std::chrono::milliseconds until(std::chrono::steady_clock::time_point due, std::chrono::steady_clock::time_point now)
{
	return due <= now ? std::chrono::milliseconds(0)
			  : std::chrono::duration_cast<std::chrono::milliseconds>(due - now);
}
} // namespace

UploaderService::UploaderService(UploaderSettings settings, UploaderPorts ports)
	: settings_(std::move(settings)), ports_(std::move(ports)), store_(settings_.token_directory),
	  home_host_(settings_.outbox.home_host_directory)
{
}

UploaderService::~UploaderService() = default;

Result<std::unique_ptr<UploaderService>> UploaderService::open(UploaderSettings settings, UploaderPorts ports)
{
	using Output = Result<std::unique_ptr<UploaderService>>;
	if (!ports.transport || !ports.resolve || !ports.utc_now || !ports.steady_now || !ports.random01 ||
	    !ports.request_id)
		return Output::failure({"VOICE_LAB.UPLOADER_CONFIG_INVALID", "The uploader lacks a port", {}, false});
	std::unique_ptr<UploaderService> service(new UploaderService(std::move(settings), std::move(ports)));
	auto *self = service.get();
	auto saved = self->store_.load(self->settings_.sign_in.issuer, self->settings_.sign_in.client_id);
	if (!saved.has_value())
		return Output::failure(saved.error());
	self->sign_in_state_ = saved.value() ? "signed_in" : "signed_out";
	AccessTokens tokens{[self]() -> Result<std::string> {
				    auto ready = self->ensure_endpoints();
				    if (!ready.has_value())
					    return Result<std::string>::failure(ready.error());
				    auto token = self->session_->access_token();
				    if (!token.has_value() && token.error().code == "VOICE_LAB.SIGN_IN_REQUIRED" &&
					self->sign_in_state_ == "signed_in")
					    self->sign_in_state_ = "signed_out";
				    return token;
			    },
			    [self] {
				    if (self->session_)
					    self->session_->invalidate_access_token();
			    }};
	self->client_ = std::make_unique<CorpusClient>(self->settings_.corpus, self->ports_.transport, tokens);
	auto outbox = Outbox::open(self->settings_.outbox, *self->client_,
				   {self->ports_.resolve, self->ports_.utc_now, self->ports_.steady_now,
				    self->ports_.random01, self->ports_.request_id});
	if (!outbox.has_value())
		return Output::failure(outbox.error());
	self->outbox_ = std::move(outbox.value());
	self->refresh_approval();
	return Output::success(std::move(service));
}

Result<bool> UploaderService::ensure_endpoints()
{
	if (!endpoints_) {
		auto found = discover(settings_.sign_in, ports_.transport);
		if (!found.has_value())
			return Result<bool>::failure(found.error());
		endpoints_ = found.value();
	}
	if (!session_) {
		session_ = std::make_unique<VoiceLabSession>(settings_.sign_in, *endpoints_, ports_.transport, store_,
							     ports_.steady_now);
		auto restored = session_->restore();
		if (!restored.has_value())
			return restored;
	}
	return Result<bool>::success(true);
}

std::vector<wire::UploaderEvent> UploaderService::handle(const wire::UiRequest &request)
{
	if (std::holds_alternative<wire::SignInRequest>(request)) {
		auto ready = ensure_endpoints();
		if (!ready.has_value())
			return {wire::Refused{"sign_in", ready.error().code}};
		auto started = DeviceCodeSignIn::start(settings_.sign_in, *endpoints_, ports_.transport, ports_.steady_now);
		if (!started.has_value())
			return {wire::Refused{"sign_in", started.error().code}};
		flow_.emplace(std::move(started.value()));
		sign_in_state_ = "pending";
	} else if (const auto *approve = std::get_if<wire::ApproveRequest>(&request)) {
		refresh_approval();
		if (!preview_)
			return {wire::Refused{"approve", "VOICE_LAB.APPROVAL_UNAVAILABLE"}};
		const auto digest = record_sha256(*preview_);
		if (!digest.has_value() || digest.value() != approve->approval_sha256)
			return {wire::Refused{"approve", "VOICE_LAB.APPROVAL_DIGEST_MISMATCH"}};
		auto approved = home_host_.approve(*preview_, digest.value(), ports_.utc_now());
		if (!approved.has_value())
			return {wire::Refused{"approve", approved.error().code}};
		preview_.reset();
		outbox_due_ = ports_.steady_now();
	} else if (const auto *keep = std::get_if<wire::KeepRequest>(&request)) {
		// Which array channels are raw is measured on the board, never assumed.
		if (!settings_.outbox.channels.is_array() || settings_.outbox.channels.empty())
			return {wire::Refused{"keep", "VOICE_LAB.CAPTURE_PROFILE_UNMEASURED"}};
		const auto prompt = find_prompt(keep->prompt_id);
		if (!prompt)
			return {wire::Refused{"keep", "VOICE_LAB.PROMPT_NOT_IN_CATALOG"}};
		auto kept = outbox_->keep({keep->take_id, *prompt, catalog_["reference"], keep->requested_at});
		if (!kept.has_value())
			return {wire::Refused{"keep", kept.error().code}};
		outbox_due_ = ports_.steady_now();
	}
	return {state()};
}

void UploaderService::refresh_approval()
{
	const std::string now = ports_.utc_now();
	if (home_host_.load(now).has_value()) {
		preview_.reset();
		return;
	}
	const auto steady = ports_.steady_now();
	if (preview_ && steady - preview_made_ < kPreviewLifetime)
		return;
	preview_.reset();
	auto peer = ports_.resolve(settings_.approval_endpoint);
	std::int64_t seconds = 0;
	if (!peer.has_value() || !parse_utc(now, seconds))
		return;
	const auto lifetime = std::chrono::duration_cast<std::chrono::seconds>(settings_.approval_lifetime).count();
	std::string id = "panel-pc-";
	for (const char c : now)
		if (c >= '0' && c <= '9')
			id += c;
	auto record = home_host_approval({id, settings_.approval_endpoint, settings_.tailnet_suffix,
					  peer.value().node_stable_id, {"capture"}, now, format_utc(seconds + lifetime),
					  settings_.approved_by, settings_.statement});
	// Preview only a node that would pass the check before every session.
	if (!record.has_value() || !check_peer(record.value(), peer.value()).has_value())
		return;
	preview_ = record.value();
	preview_made_ = steady;
}

void UploaderService::refresh_views()
{
	auto catalog = client_->catalog(catalog_etag_);
	if (!catalog.has_value()) {
		error_ = catalog.error().code;
		return;
	}
	if (!catalog.value().not_modified) {
		catalog_ = catalog.value().body;
		catalog_etag_ = catalog.value().etag;
	}
	auto library = client_->library(library_etag_);
	if (!library.has_value()) {
		error_ = library.error().code;
		return;
	}
	if (!library.value().not_modified) {
		library_ = library.value().body;
		library_etag_ = library.value().etag;
	}
	error_.clear();
}

std::optional<Json> UploaderService::find_prompt(const std::string &prompt_id) const
{
	if (!catalog_.is_object() || !catalog_.contains("suite") || !catalog_["suite"].contains("prompts"))
		return std::nullopt;
	for (const auto &prompt : catalog_["suite"]["prompts"])
		if (prompt.value("prompt_id", std::string()) == prompt_id)
			return prompt;
	return std::nullopt;
}

std::chrono::milliseconds UploaderService::tick()
{
	auto now = ports_.steady_now();
	std::chrono::milliseconds wait = kIdlePoll;
	if (flow_) {
		auto polled = flow_->poll();
		if (!polled.has_value()) {
			error_ = polled.error().code;
			flow_.reset();
			sign_in_state_ = "signed_out";
		} else if (polled.value().state == DeviceCodeSignIn::State::Approved && polled.value().tokens) {
			flow_.reset();
			auto ready = ensure_endpoints();
			auto adopted = ready.has_value() ? session_->adopt(*polled.value().tokens)
							 : Result<bool>::failure(ready.error());
			sign_in_state_ = adopted.has_value() ? "signed_in" : "signed_out";
			if (!adopted.has_value())
				error_ = adopted.error().code;
			views_due_ = now;
			outbox_due_ = now;
		} else if (polled.value().state == DeviceCodeSignIn::State::Expired) {
			flow_.reset();
			sign_in_state_ = "expired";
		} else {
			wait = std::min<std::chrono::milliseconds>(wait, std::chrono::seconds(1));
		}
	}
	refresh_approval();
	now = ports_.steady_now();
	if (sign_in_state_ == "signed_in" && now >= views_due_) {
		refresh_views();
		views_due_ = now + settings_.catalog_refresh;
	}
	if (now >= outbox_due_) {
		const auto step = outbox_->step();
		switch (step.kind) {
		case OutboxStep::Kind::Progressed:
			// A take moved on: its prompt's status in the library may have too.
			outbox_due_ = now;
			views_due_ = now;
			break;
		case OutboxStep::Kind::Waiting:
			outbox_due_ = now + step.retry_after;
			break;
		default:
			outbox_due_ = now + kIdlePoll;
		}
	}
	if (sign_in_state_ == "signed_in")
		wait = std::min(wait, until(views_due_, now));
	return std::min(wait, until(outbox_due_, now));
}

wire::State UploaderService::state()
{
	wire::State current;
	current.sign_in.state = sign_in_state_;
	if (flow_ && sign_in_state_ == "pending") {
		const auto &prompt = flow_->prompt();
		current.sign_in.verification_uri = prompt.verification_uri;
		current.sign_in.user_code = prompt.user_code;
		const auto left = std::chrono::duration_cast<std::chrono::seconds>(prompt.expires_at - ports_.steady_now());
		current.sign_in.expires_in_seconds = static_cast<std::uint64_t>(std::max<std::int64_t>(0, left.count()));
	}
	if (auto approval = home_host_.load(ports_.utc_now()); approval.has_value()) {
		current.approval = {"in_force", approval.value().approval["endpoint"],
				    approval.value().approval["node_stable_id"], approval.value().approval["expires_at"],
				    approval.value().sha256};
	} else if (preview_) {
		current.approval = {"preview", (*preview_)["endpoint"], (*preview_)["node_stable_id"],
				    (*preview_)["expires_at"], record_sha256(*preview_).value()};
	}
	const Json outbox = outbox_->status();
	for (const auto &take : outbox["takes"]) {
		const std::string stage = take.value("stage", std::string());
		(stage == "kept" ? current.kept : stage == "refused" ? current.refused : current.queued) += 1;
	}
	current.blocked = outbox["blocked"].is_string() ? outbox["blocked"].get<std::string>() : std::string();
	if (catalog_.is_object() && catalog_.contains("reference"))
		current.catalog_version = catalog_["reference"].value("catalog_version", std::uint64_t{0});
	std::map<std::string, std::string> status;
	if (library_.is_object())
		for (const auto &row : library_.value("rows", Json::array()))
			status[row.value("prompt_id", std::string())] = row.value("status", std::string("missing"));
	if (catalog_.is_object() && catalog_.contains("suite"))
		for (const auto &prompt : catalog_["suite"].value("prompts", Json::array())) {
			if (current.prompts.size() >= wire::kMaximumPrompts)
				break;
			const std::string id = prompt.value("prompt_id", std::string());
			const auto found = status.find(id);
			current.prompts.push_back({id, clip(prompt.value("text", std::string()), kMaximumPromptText),
						   clip(prompt.value("category", std::string()), 64),
						   found == status.end() ? "missing" : found->second});
		}
	current.error = error_;
	// The sequence rises only when something the UI shows changed.
	current.sequence = last_.sequence;
	const auto now_frame = wire::encode(wire::UploaderEvent(current));
	const auto last_frame = wire::encode(wire::UploaderEvent(last_));
	if (!now_frame.has_value() || !last_frame.has_value() || now_frame.value() != last_frame.value()) {
		current.sequence = last_.sequence + 1;
		last_ = current;
	}
	return current;
}
} // namespace voice_lab_recorder
