#include "voice_lab_recorder/outbox.h"
#include "voice_lab_recorder/canonical_json.h"
#include "voice_lab_recorder/take_package.h"
#include <algorithm>
#include <cstdio>
#include <dirent.h>
#include <unistd.h>

namespace voice_lab_recorder
{
namespace
{
using Json = nlohmann::json;
constexpr std::size_t kMaximumStateBytes = 4 << 20;
constexpr int kSchemaVersion = 1;

template <typename Value> Result<Value> fail(const char *code, const char *message)
{
	return Result<Value>::failure({code, message, {}, false});
}

void remove_handoff(const std::string &directory) noexcept
{
	for (const char *name : {"original.wav", "capture.json"})
		::unlink((directory + "/" + name).c_str());
	::rmdir(directory.c_str());
}

bool unfinished(const Json &entry)
{
	const std::string stage = entry.value("stage", std::string());
	return stage != "kept" && stage != "refused";
}

std::string refusal_of(const Error &error)
{
	const auto found = error.context.find("error");
	return found == error.context.end() ? std::string() : found->second;
}
} // namespace

Outbox::Outbox(OutboxSettings settings, CorpusClient &client, OutboxPorts ports)
	: settings_(std::move(settings)), client_(client), ports_(std::move(ports)),
	  file_(settings_.outbox_directory, {"outbox.lock", "outbox.json", "outbox.tmp", "VOICE_LAB"}, kMaximumStateBytes),
	  home_host_(settings_.home_host_directory)
{
}

Result<std::unique_ptr<Outbox>> Outbox::open(OutboxSettings settings, CorpusClient &client, OutboxPorts ports)
{
	using Output = Result<std::unique_ptr<Outbox>>;
	auto backoff = bhp::durable_store::Backoff::create(settings.backoff, ports.random01);
	if (!backoff.has_value() || !ports.resolve || !ports.utc_now || !ports.steady_now || !ports.request_id)
		return fail<std::unique_ptr<Outbox>>("VOICE_LAB.OUTBOX_CONFIG_INVALID", "The outbox lacks a port or policy");
	std::unique_ptr<Outbox> outbox(new Outbox(std::move(settings), client, std::move(ports)));
	outbox->backoff_ = std::make_unique<bhp::durable_store::Backoff>(backoff.value());
	auto read = outbox->file_.read();
	if (!read.has_value())
		return Output::failure({read.error().code, read.error().message, read.error().context, false});
	if (read.value()) {
		outbox->state_ = Json::parse(*read.value(), nullptr, false);
		if (!outbox->state_.is_object() || outbox->state_.value("schema_version", 0) != kSchemaVersion ||
		    !outbox->state_.contains("takes") || !outbox->state_["takes"].is_array())
			return fail<std::unique_ptr<Outbox>>("VOICE_LAB.OUTBOX_STATE_INVALID",
							     "outbox.json is not a version 1 record");
	} else {
		outbox->state_ = {{"schema_version", kSchemaVersion}, {"takes", Json::array()}};
	}
	return Output::success(std::move(outbox));
}

Result<bool> Outbox::save()
{
	auto committed = file_.commit(state_.dump() + "\n");
	if (!committed.has_value())
		return Result<bool>::failure({committed.error().code, committed.error().message, committed.error().context, false});
	return Result<bool>::success(true);
}

Result<Json> Outbox::keep(const KeepRequest &request)
{
	for (const auto &entry : state_["takes"])
		if (entry.value("take_id", std::string()) == request.take_id)
			return Result<Json>::success(entry);
	const auto waiting = std::count_if(state_["takes"].begin(), state_["takes"].end(), unfinished);
	if (static_cast<std::size_t>(waiting) >= settings_.maximum_takes)
		return fail<Json>("VOICE_LAB.OUTBOX_FULL", "The outbox holds as many takes as it may");
	const std::string handoff = settings_.handoff_directory + "/" + request.take_id;
	auto capture = [&]() -> std::optional<Json> {
		FILE *file = std::fopen((handoff + "/capture.json").c_str(), "rb");
		if (!file)
			return std::nullopt;
		std::string text;
		char buffer[4096];
		size_t got;
		while ((got = std::fread(buffer, 1, sizeof buffer, file)) > 0 && text.size() < kMaximumStateBytes)
			text.append(buffer, got);
		std::fclose(file);
		Json value = Json::parse(text, nullptr, false);
		return value.is_object() ? std::optional<Json>(value) : std::nullopt;
	}();
	if (!capture)
		return fail<Json>("VOICE_LAB.HANDOFF_MISSING", "The broker has not handed this take over");
	auto profile = xvf3800_capture_profile(capture->value("device", Json()), settings_.channels, settings_.room,
					       settings_.primary_channel);
	if (!profile.has_value())
		return Result<Json>::failure(profile.error());
	TakePackageInputs inputs;
	inputs.prompt = request.prompt;
	inputs.catalog = request.catalog;
	inputs.capture_profile = profile.value();
	inputs.client = settings_.client;
	inputs.capture_host = settings_.capture_host;
	inputs.requested_at = request.requested_at;
	inputs.qc_generated_at = ports_.utc_now();
	inputs.finalized_at = inputs.qc_generated_at;
	auto packaged = write_take_package(handoff, settings_.outbox_directory, inputs);
	if (!packaged.has_value() && packaged.error().code != "VOICE_LAB.TAKE_PACKAGE_EXISTS")
		return Result<Json>::failure(packaged.error());
	const std::string directory = settings_.outbox_directory + "/" + request.take_id;
	auto session = intake_session_request(directory);
	if (!session.has_value())
		return Result<Json>::failure(session.error());
	Json entry = {{"take_id", request.take_id},
		      {"prompt_id", request.prompt.value("prompt_id", std::string())},
		      {"manifest_sha256", session.value()["manifest_sha256"]},
		      {"stage", "queued"},
		      {"queued_at", ports_.utc_now()},
		      {"request_id", nullptr},
		      {"error", nullptr}};
	state_["takes"].push_back(entry);
	auto saved = save();
	if (!saved.has_value()) {
		state_["takes"].erase(state_["takes"].size() - 1);
		return Result<Json>::failure(saved.error());
	}
	// The package and its record are durable: the hand-off copy is no longer needed.
	remove_handoff(handoff);
	return Result<Json>::success(entry);
}

Result<BoundApproval> Outbox::standing_approval()
{
	auto approval = home_host_.load(ports_.utc_now());
	if (!approval.has_value())
		return approval;
	const Json classes = approval.value().approval.value("classes", Json::array());
	if (std::find(classes.begin(), classes.end(), "capture") == classes.end())
		return fail<BoundApproval>("VOICE_LAB.APPROVAL_NOT_FOR_CAPTURE", "The approval does not cover takes");
	auto peer = ports_.resolve(approval.value().approval.value("endpoint", std::string()));
	if (!peer.has_value())
		return Result<BoundApproval>::failure(peer.error());
	auto matches = check_peer(approval.value().approval, peer.value());
	if (!matches.has_value())
		return Result<BoundApproval>::failure(matches.error());
	return approval;
}

OutboxStep Outbox::block(const std::string &reason)
{
	blocked_ = reason;
	return {OutboxStep::Kind::Blocked, settings_.backoff.maximum, reason};
}

OutboxStep Outbox::retry(Json &entry, const Error &error)
{
	entry["error"] = error.code;
	(void)save();
	const auto delay = backoff_->next_delay();
	not_before_ = ports_.steady_now() + delay;
	return {OutboxStep::Kind::Waiting, delay, error.code};
}

OutboxStep Outbox::refuse(Json &entry, const Error &error)
{
	auto approval = home_host_.load(ports_.utc_now());
	if (approval.has_value()) {
		const std::string detail = (error.code + " " + refusal_of(error)).substr(0, 2000);
		(void)home_host_.append(approval.value(), {"upload-refused", ports_.utc_now(), entry["take_id"],
							   entry["manifest_sha256"], {}, detail});
	}
	entry["stage"] = "refused";
	entry["error"] = refusal_of(error).empty() ? error.code : refusal_of(error);
	(void)save();
	return {OutboxStep::Kind::Progressed, std::chrono::milliseconds(0), {}};
}

OutboxStep Outbox::decision_refused(Json &entry, const Error &error)
{
	// The take is on the PC; only the keep failed. It stays a draft there.
	entry["stage"] = "refused";
	entry["error"] = refusal_of(error).empty() ? error.code : refusal_of(error);
	(void)save();
	return {OutboxStep::Kind::Progressed, std::chrono::milliseconds(0), {}};
}

OutboxStep Outbox::transfer(Json &entry)
{
	const std::string take_id = entry["take_id"];
	auto approval = standing_approval();
	if (!approval.has_value())
		return block(approval.error().code);
	if (!started_this_run_.count(take_id)) {
		auto started = home_host_.append(approval.value(), {"upload-started", ports_.utc_now(), take_id,
								    entry["manifest_sha256"], {}, {}});
		if (!started.has_value())
			return block(started.error().code);
		started_this_run_.insert(take_id);
	}
	auto finalized = client_.upload(settings_.outbox_directory + "/" + take_id);
	if (!finalized.has_value()) {
		if (finalized.error().code == "VOICE_LAB.SIGN_IN_REQUIRED" ||
		    finalized.error().code == "VOICE_LAB.API_UNAUTHORIZED")
			return block("VOICE_LAB.SIGN_IN_REQUIRED");
		if (finalized.error().retryable)
			return retry(entry, finalized.error());
		return refuse(entry, finalized.error());
	}
	entry["stage"] = "uploaded";
	entry["error"] = nullptr;
	(void)save();
	return await_outcome(entry);
}

OutboxStep Outbox::await_outcome(Json &entry)
{
	const std::string take_id = entry["take_id"];
	auto status = client_.session(take_id);
	if (!status.has_value()) {
		if (status.error().code == "VOICE_LAB.SIGN_IN_REQUIRED" || status.error().code == "VOICE_LAB.API_UNAUTHORIZED")
			return block("VOICE_LAB.SIGN_IN_REQUIRED");
		return status.error().retryable ? retry(entry, status.error()) : refuse(entry, status.error());
	}
	const std::string state = status.value().value("state", std::string());
	if (state == "refused") {
		const auto refusal = status.value().value("refusal", Json());
		Error refused{"VOICE_LAB.TAKE_REFUSED", "voice-lab refused the take", {}, false};
		if (refusal.is_string())
			refused.context["error"] = refusal.get<std::string>().substr(0, 1900);
		return refuse(entry, refused);
	}
	if (state == "open") {
		entry["stage"] = "queued"; // a file is missing: send it again
		(void)save();
		return {OutboxStep::Kind::Progressed, std::chrono::milliseconds(0), {}};
	}
	if (state != "accepted") {
		const auto delay = backoff_->next_delay();
		not_before_ = ports_.steady_now() + delay;
		return {OutboxStep::Kind::Waiting, delay, "VOICE_LAB.INTAKE_PENDING"};
	}
	const Json receipt = status.value().value("receipt", Json());
	if (!receipt.is_object() || receipt.value("session_id", std::string()) != take_id ||
	    receipt.value("manifest_sha256", std::string()) != entry["manifest_sha256"].get<std::string>())
		return refuse(entry, {"VOICE_LAB.RECEIPT_INVALID", "The receipt does not name this package", {}, false});
	auto digest = record_sha256(receipt);
	auto approval = home_host_.load(ports_.utc_now());
	if (!digest.has_value() || !approval.has_value())
		return block(approval.has_value() ? digest.error().code : approval.error().code);
	auto completed = home_host_.append(approval.value(), {"upload-completed", ports_.utc_now(), take_id,
							      entry["manifest_sha256"], digest.value(), {}});
	if (!completed.has_value())
		return block(completed.error().code);
	entry["stage"] = "accepted";
	entry["receipt_sha256"] = digest.value();
	entry["error"] = nullptr;
	(void)save();
	backoff_->reset();
	return {OutboxStep::Kind::Progressed, std::chrono::milliseconds(0), {}};
}

OutboxStep Outbox::decide(Json &entry)
{
	const std::string take_id = entry["take_id"];
	if (entry["request_id"].is_string()) {
		const std::string request_id = entry["request_id"];
		auto status = client_.request_status(request_id);
		if (!status.has_value()) {
			const auto found = status.error().context.find("status");
			// Saved but never received (a crash before the send): send the same change.
			if (found != status.error().context.end() && found->second == "404") {
				auto resent = client_.decide(take_id, {request_id, "kept",
								       entry.value("expected_revision", std::uint64_t{0}),
								       std::nullopt});
				if (!resent.has_value())
					return resent.error().retryable ? retry(entry, resent.error())
									: decision_refused(entry, resent.error());
				return {OutboxStep::Kind::Progressed, std::chrono::milliseconds(0), {}};
			}
			return status.error().retryable ? retry(entry, status.error())
							: decision_refused(entry, status.error());
		}
		const std::string answer = status.value().value("status", std::string());
		if (answer == "applied") {
			entry["stage"] = "kept";
			entry["error"] = nullptr;
			(void)save();
			backoff_->reset();
			return {OutboxStep::Kind::Progressed, std::chrono::milliseconds(0), {}};
		}
		if (answer == "refused") {
			const Json outcome = status.value().value("outcome", Json::object());
			// Someone decided first (the desktop): read the history and decide again.
			if (outcome.is_object() && outcome.value("code", std::string()) == "revision_conflict" &&
			    entry.value("conflicts", 0) < 2) {
				entry["conflicts"] = entry.value("conflicts", 0) + 1;
				entry["request_id"] = nullptr;
				(void)save();
				return {OutboxStep::Kind::Progressed, std::chrono::milliseconds(0), {}};
			}
			Error refused{"VOICE_LAB.DECISION_REFUSED", "voice-lab refused the decision", {}, false};
			if (outcome.is_object())
				refused.context["error"] = outcome.value("code", std::string());
			return decision_refused(entry, refused);
		}
		const auto delay = backoff_->next_delay();
		not_before_ = ports_.steady_now() + delay;
		return {OutboxStep::Kind::Waiting, delay, "VOICE_LAB.DECISION_QUEUED"};
	}
	auto history = client_.history(entry.value("prompt_id", std::string()));
	if (!history.has_value())
		return history.error().retryable ? retry(entry, history.error()) : decision_refused(entry, history.error());
	std::optional<Json> take;
	for (const auto &item : history.value().body.value("takes", Json::array()))
		if (item.value("take_id", std::string()) == take_id)
			take = item;
	if (!take) {
		// Published moments after acceptance: ask again shortly (PROTOCOL.md section 6).
		const auto delay = backoff_->next_delay();
		not_before_ = ports_.steady_now() + delay;
		return {OutboxStep::Kind::Waiting, delay, "VOICE_LAB.TAKE_NOT_PUBLISHED"};
	}
	if (take->value("decision", std::string()) == "kept") {
		entry["stage"] = "kept";
		(void)save();
		return {OutboxStep::Kind::Progressed, std::chrono::milliseconds(0), {}};
	}
	auto id = ports_.request_id();
	if (!id.has_value())
		return block(id.error().code);
	// The request id is durable before the first send, so a retry is the same change.
	entry["request_id"] = id.value();
	entry["expected_revision"] = take->value("decision_revision", std::uint64_t{0});
	auto saved = save();
	if (!saved.has_value())
		return block(saved.error().code);
	auto queued = client_.decide(take_id, {id.value(), "kept", entry["expected_revision"].get<std::uint64_t>(),
					       std::nullopt});
	if (!queued.has_value())
		return queued.error().retryable ? retry(entry, queued.error()) : decision_refused(entry, queued.error());
	return {OutboxStep::Kind::Progressed, std::chrono::milliseconds(0), {}};
}

OutboxStep Outbox::step()
{
	if (ports_.steady_now() < not_before_)
		return {OutboxStep::Kind::Waiting,
			std::chrono::duration_cast<std::chrono::milliseconds>(not_before_ - ports_.steady_now()), {}};
	blocked_.clear();
	for (auto &entry : state_["takes"]) {
		if (!unfinished(entry))
			continue;
		const std::string stage = entry.value("stage", std::string());
		if (stage == "queued")
			return transfer(entry);
		if (stage == "uploaded")
			return await_outcome(entry);
		if (stage == "accepted")
			return decide(entry);
		return refuse(entry, {"VOICE_LAB.OUTBOX_STATE_INVALID", "An outbox take has an unknown stage", {}, false});
	}
	return {OutboxStep::Kind::Idle, std::chrono::milliseconds(0), {}};
}

Json Outbox::status() const
{
	Json takes = Json::array();
	for (const auto &entry : state_["takes"])
		takes.push_back({{"take_id", entry["take_id"]},
				 {"prompt_id", entry["prompt_id"]},
				 {"stage", entry["stage"]},
				 {"error", entry.value("error", Json())}});
	return {{"blocked", blocked_.empty() ? Json(nullptr) : Json(blocked_)}, {"takes", takes}};
}
} // namespace voice_lab_recorder
