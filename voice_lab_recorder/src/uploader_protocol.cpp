#include "voice_lab_recorder/uploader_protocol.h"
#include "bhp/media_window_protocol/media_window_protocol.h"
#include <algorithm>
#include <nlohmann/json.hpp>
#include <regex>
#include <set>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace voice_lab_recorder::uploader_protocol
{
namespace
{
using Json = nlohmann::json;

Error wire_error(const std::string &where)
{
	return {"VOICE_LAB.UPLOADER_PROTOCOL_INVALID", "The uploader frame is not valid", {{"where", where}}, false};
}

template <typename Value> Result<Value> fail(const std::string &where)
{
	return Result<Value>::failure(wire_error(where));
}

bool printable(const std::string &text, std::size_t maximum, bool allow_empty = false)
{
	if (text.size() > maximum || (!allow_empty && text.empty()))
		return false;
	return std::all_of(text.begin(), text.end(), [](unsigned char c) { return c >= 0x20 && c != 0x7f; });
}

bool sha256(const std::string &value)
{
	static const std::regex pattern("[0-9a-f]{64}");
	return std::regex_match(value, pattern);
}

bool take_id(const std::string &value)
{
	static const std::regex pattern("take-[0-9a-f]{64}");
	return std::regex_match(value, pattern);
}

bool stable_id(const std::string &value)
{
	static const std::regex pattern("[a-z0-9](?:[a-z0-9._-]{0,126}[a-z0-9])?");
	return value.size() <= 128 && std::regex_match(value, pattern);
}

/** RFC 3339 UTC as the records write it; the uploader parses it fully. */
bool timestamp(const std::string &value)
{
	static const std::regex pattern(R"(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(\.\d{1,9})?Z)");
	return std::regex_match(value, pattern);
}

bool code(const std::string &value)
{
	static const std::regex pattern("[A-Z][A-Z0-9_]*(\\.[A-Za-z0-9_.-]+)*");
	return value.empty() || (value.size() <= 96 && std::regex_match(value, pattern));
}

bool one_of(const std::string &value, std::initializer_list<const char *> allowed)
{
	return std::any_of(allowed.begin(), allowed.end(), [&value](const char *item) { return value == item; });
}

bool exact_keys(const Json &value, std::initializer_list<const char *> keys)
{
	if (!value.is_object() || value.size() != keys.size())
		return false;
	return std::all_of(keys.begin(), keys.end(), [&value](const char *key) { return value.contains(key); });
}

Result<std::string> finish(Json value, const char *type)
{
	value["type"] = type;
	value["version"] = kProtocolVersion;
	auto text = value.dump(-1, ' ', false, Json::error_handler_t::strict) + "\n";
	if (text.size() > kMaximumFrameBytes)
		return fail<std::string>(std::string("encode_") + type + "_size");
	return Result<std::string>::success(std::move(text));
}

Result<Json> parse(std::string_view frame, const char *where)
{
	if (frame.empty() || frame.size() > kMaximumFrameBytes || frame.back() != '\n' ||
	    frame.find('\n') != frame.size() - 1)
		return fail<Json>(where);
	Json value = Json::parse(frame.substr(0, frame.size() - 1), nullptr, false);
	if (value.is_discarded() || !value.is_object() || !value.contains("type") || !value["type"].is_string() ||
	    !value.contains("version") || !value["version"].is_number_unsigned() || value["version"] != kProtocolVersion)
		return fail<Json>(where);
	return Result<Json>::success(std::move(value));
}

bool read(const Json &value, const char *key, std::string &out)
{
	if (!value.contains(key) || !value[key].is_string())
		return false;
	out = value[key].get<std::string>();
	return true;
}

bool read(const Json &value, const char *key, std::uint64_t &out)
{
	if (!value.contains(key) || !value[key].is_number_unsigned())
		return false;
	out = value[key].get<std::uint64_t>();
	return true;
}

bool valid(const SignIn &sign_in)
{
	if (!one_of(sign_in.state, {"signed_in", "signed_out", "pending", "expired"}))
		return false;
	if (sign_in.state != "pending")
		return sign_in.verification_uri.empty() && sign_in.user_code.empty() && sign_in.expires_in_seconds == 0;
	return sign_in.verification_uri.rfind("https://", 0) == 0 && printable(sign_in.verification_uri, 512) &&
	       printable(sign_in.user_code, 32) && sign_in.expires_in_seconds <= 3600;
}

bool valid(const Approval &approval)
{
	if (!one_of(approval.state, {"in_force", "preview", "unavailable"}))
		return false;
	if (approval.state == "unavailable")
		return approval.endpoint.empty() && approval.node_stable_id.empty() && approval.expires_at.empty() &&
		       approval.approval_sha256.empty();
	return printable(approval.endpoint, 253) && printable(approval.node_stable_id, 64) &&
	       timestamp(approval.expires_at) && sha256(approval.approval_sha256);
}

bool valid(const Prompt &prompt)
{
	return stable_id(prompt.prompt_id) && printable(prompt.text, 2048, true) && printable(prompt.category, 64) &&
	       one_of(prompt.status, {"missing", "draft", "kept", "needs-attention"});
}

bool valid(const State &state)
{
	return valid(state.sign_in) && valid(state.approval) && code(state.blocked) && code(state.error) &&
	       state.prompts.size() <= kMaximumPrompts &&
	       std::all_of(state.prompts.begin(), state.prompts.end(), [](const Prompt &p) { return valid(p); });
}
} // namespace

Result<std::string> encode(const UiRequest &request)
{
	if (std::holds_alternative<StatusRequest>(request))
		return finish(Json::object(), "status");
	if (std::holds_alternative<SignInRequest>(request))
		return finish(Json::object(), "sign_in");
	if (const auto *approve = std::get_if<ApproveRequest>(&request)) {
		if (!sha256(approve->approval_sha256))
			return fail<std::string>("encode_approve");
		return finish({{"approval_sha256", approve->approval_sha256}}, "approve");
	}
	const auto &keep = std::get<KeepRequest>(request);
	if (!take_id(keep.take_id) || !stable_id(keep.prompt_id) || !timestamp(keep.requested_at))
		return fail<std::string>("encode_keep");
	return finish({{"take_id", keep.take_id}, {"prompt_id", keep.prompt_id}, {"requested_at", keep.requested_at}},
		      "keep");
}

Result<std::string> encode(const UploaderEvent &event)
{
	if (const auto *refused = std::get_if<Refused>(&event)) {
		if (!one_of(refused->request, {"sign_in", "approve", "keep"}) || refused->error_code.empty() ||
		    !code(refused->error_code))
			return fail<std::string>("encode_refused");
		return finish({{"request", refused->request}, {"error_code", refused->error_code}}, "refused");
	}
	const auto &state = std::get<State>(event);
	if (!valid(state))
		return fail<std::string>("encode_state");
	Json prompts = Json::array();
	for (const auto &prompt : state.prompts)
		prompts.push_back({{"prompt_id", prompt.prompt_id},
				   {"text", prompt.text},
				   {"category", prompt.category},
				   {"status", prompt.status}});
	const bool pending = state.sign_in.state == "pending";
	const bool approval = state.approval.state != "unavailable";
	return finish(
		{{"sequence", state.sequence},
		 {"sign_in",
		  {{"state", state.sign_in.state},
		   {"verification_uri", pending ? Json(state.sign_in.verification_uri) : Json(nullptr)},
		   {"user_code", pending ? Json(state.sign_in.user_code) : Json(nullptr)},
		   {"expires_in_seconds", pending ? Json(state.sign_in.expires_in_seconds) : Json(nullptr)}}},
		 {"approval",
		  {{"state", state.approval.state},
		   {"endpoint", approval ? Json(state.approval.endpoint) : Json(nullptr)},
		   {"node_stable_id", approval ? Json(state.approval.node_stable_id) : Json(nullptr)},
		   {"expires_at", approval ? Json(state.approval.expires_at) : Json(nullptr)},
		   {"approval_sha256", approval ? Json(state.approval.approval_sha256) : Json(nullptr)}}},
		 {"outbox",
		  {{"queued", state.queued},
		   {"kept", state.kept},
		   {"refused", state.refused},
		   {"blocked", state.blocked.empty() ? Json(nullptr) : Json(state.blocked)}}},
		 {"catalog_version", state.catalog_version ? Json(*state.catalog_version) : Json(nullptr)},
		 {"prompts", prompts},
		 {"error", state.error.empty() ? Json(nullptr) : Json(state.error)}},
		"state");
}

Result<UiRequest> decode_ui_request(std::string_view frame)
{
	auto parsed = parse(frame, "decode_ui_request");
	if (!parsed.has_value())
		return Result<UiRequest>::failure(parsed.error());
	const Json &value = parsed.value();
	const std::string type = value["type"];
	if ((type == "status" || type == "sign_in") && exact_keys(value, {"type", "version"}))
		return Result<UiRequest>::success(type == "status" ? UiRequest(StatusRequest{}) : UiRequest(SignInRequest{}));
	if (type == "approve") {
		ApproveRequest approve;
		if (!exact_keys(value, {"type", "version", "approval_sha256"}) ||
		    !read(value, "approval_sha256", approve.approval_sha256) || !encode(UiRequest(approve)).has_value())
			return fail<UiRequest>("decode_approve");
		return Result<UiRequest>::success(approve);
	}
	if (type == "keep") {
		KeepRequest keep;
		if (!exact_keys(value, {"type", "version", "take_id", "prompt_id", "requested_at"}) ||
		    !read(value, "take_id", keep.take_id) || !read(value, "prompt_id", keep.prompt_id) ||
		    !read(value, "requested_at", keep.requested_at) || !encode(UiRequest(keep)).has_value())
			return fail<UiRequest>("decode_keep");
		return Result<UiRequest>::success(keep);
	}
	return fail<UiRequest>("decode_ui_request_type");
}

Result<UploaderEvent> decode_uploader_event(std::string_view frame)
{
	auto parsed = parse(frame, "decode_uploader_event");
	if (!parsed.has_value())
		return Result<UploaderEvent>::failure(parsed.error());
	const Json &value = parsed.value();
	const std::string type = value["type"];
	if (type == "refused") {
		Refused refused;
		if (!exact_keys(value, {"type", "version", "request", "error_code"}) ||
		    !read(value, "request", refused.request) || !read(value, "error_code", refused.error_code) ||
		    !encode(UploaderEvent(refused)).has_value())
			return fail<UploaderEvent>("decode_refused");
		return Result<UploaderEvent>::success(refused);
	}
	if (type != "state" ||
	    !exact_keys(value, {"type", "version", "sequence", "sign_in", "approval", "outbox", "catalog_version",
				"prompts", "error"}))
		return fail<UploaderEvent>("decode_uploader_event_type");
	State state;
	const Json &sign_in = value["sign_in"];
	const Json &approval = value["approval"];
	const Json &outbox = value["outbox"];
	auto optional_string = [](const Json &object, const char *key, std::string &out) {
		if (!object.contains(key))
			return false;
		if (object[key].is_null())
			return true;
		if (!object[key].is_string())
			return false;
		out = object[key].get<std::string>();
		return true;
	};
	auto optional_number = [](const Json &object, const char *key, std::uint64_t &out) {
		if (!object.contains(key))
			return false;
		if (object[key].is_null())
			return true;
		if (!object[key].is_number_unsigned())
			return false;
		out = object[key].get<std::uint64_t>();
		return true;
	};
	bool ok = read(value, "sequence", state.sequence) &&
		  exact_keys(sign_in, {"state", "verification_uri", "user_code", "expires_in_seconds"}) &&
		  read(sign_in, "state", state.sign_in.state) &&
		  optional_string(sign_in, "verification_uri", state.sign_in.verification_uri) &&
		  optional_string(sign_in, "user_code", state.sign_in.user_code) &&
		  optional_number(sign_in, "expires_in_seconds", state.sign_in.expires_in_seconds) &&
		  exact_keys(approval, {"state", "endpoint", "node_stable_id", "expires_at", "approval_sha256"}) &&
		  read(approval, "state", state.approval.state) &&
		  optional_string(approval, "endpoint", state.approval.endpoint) &&
		  optional_string(approval, "node_stable_id", state.approval.node_stable_id) &&
		  optional_string(approval, "expires_at", state.approval.expires_at) &&
		  optional_string(approval, "approval_sha256", state.approval.approval_sha256) &&
		  exact_keys(outbox, {"queued", "kept", "refused", "blocked"}) && read(outbox, "queued", state.queued) &&
		  read(outbox, "kept", state.kept) && read(outbox, "refused", state.refused) &&
		  optional_string(outbox, "blocked", state.blocked) && optional_string(value, "error", state.error) &&
		  value["prompts"].is_array() && value["prompts"].size() <= kMaximumPrompts;
	std::uint64_t version = 0;
	if (ok && !value["catalog_version"].is_null()) {
		ok = read(value, "catalog_version", version);
		state.catalog_version = version;
	}
	for (std::size_t at = 0; ok && at < value["prompts"].size(); ++at) {
		const Json &item = value["prompts"][at];
		Prompt prompt;
		ok = exact_keys(item, {"prompt_id", "text", "category", "status"}) && read(item, "prompt_id", prompt.prompt_id) &&
		     read(item, "text", prompt.text) && read(item, "category", prompt.category) &&
		     read(item, "status", prompt.status);
		state.prompts.push_back(std::move(prompt));
	}
	if (!ok || !encode(UploaderEvent(state)).has_value())
		return fail<UploaderEvent>("decode_state");
	return Result<UploaderEvent>::success(std::move(state));
}
struct UploaderClient::Channel {
	explicit Channel(int descriptor) : frames(descriptor, kMaximumFrameBytes) {}
	bhp::media_window_protocol::FrameChannel frames;
};

UploaderClient::UploaderClient(int descriptor) : channel_(std::make_unique<Channel>(descriptor)) {}
UploaderClient::~UploaderClient() = default;

Result<std::unique_ptr<UploaderClient>> UploaderClient::connect(const std::string &path, std::uint32_t uploader_uid)
{
	using Output = Result<std::unique_ptr<UploaderClient>>;
	if (path.empty() || path[0] != '/' || path.size() >= sizeof(sockaddr_un::sun_path))
		return Output::failure({"VOICE_LAB.UPLOADER_SOCKET_INVALID", "The uploader socket path is invalid", {}, false});
	const int descriptor = ::socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
	sockaddr_un address{};
	address.sun_family = AF_UNIX;
	std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
	if (descriptor < 0 || ::connect(descriptor, reinterpret_cast<sockaddr *>(&address), sizeof address) != 0) {
		if (descriptor >= 0)
			::close(descriptor);
		return Output::failure({"VOICE_LAB.UPLOADER_UNAVAILABLE", "The uploader socket did not accept", {}, true});
	}
	return for_connected_peer(descriptor, uploader_uid);
}

Result<std::unique_ptr<UploaderClient>> UploaderClient::for_connected_peer(int descriptor, std::uint32_t uploader_uid)
{
	using Output = Result<std::unique_ptr<UploaderClient>>;
	const auto uid = bhp::media_window_protocol::peer_uid(descriptor);
	if (!uid || *uid != uploader_uid) {
		::close(descriptor);
		return Output::failure({"VOICE_LAB.UPLOADER_PEER_REJECTED", "The socket's peer is not the uploader UID", {}, false});
	}
	return Output::success(std::unique_ptr<UploaderClient>(new UploaderClient(descriptor)));
}

bool UploaderClient::connected() const
{
	return channel_->frames.open();
}

Result<bool> UploaderClient::send(const UiRequest &request)
{
	auto frame = encode(request);
	if (!frame.has_value())
		return Result<bool>::failure(frame.error());
	if (!channel_->frames.send(frame.value()))
		return Result<bool>::failure({"VOICE_LAB.UPLOADER_UNAVAILABLE", "The uploader went away", {}, true});
	return Result<bool>::success(true);
}

Result<std::vector<UploaderEvent>> UploaderClient::poll()
{
	using Output = Result<std::vector<UploaderEvent>>;
	std::vector<UploaderEvent> events;
	for (int frames = 0; frames < 16; ++frames) {
		auto received = channel_->frames.receive();
		if (!received)
			return Output::failure({"VOICE_LAB.UPLOADER_UNAVAILABLE", "The uploader went away", {}, true});
		if (!received.value())
			break;
		auto event = decode_uploader_event(*received.value());
		if (!event.has_value()) {
			channel_->frames.close();
			return Output::failure({"VOICE_LAB.UPLOADER_UNAVAILABLE", "The uploader sent a malformed frame", {}, true});
		}
		events.push_back(std::move(event.value()));
	}
	return Output::success(std::move(events));
}
} // namespace voice_lab_recorder::uploader_protocol
