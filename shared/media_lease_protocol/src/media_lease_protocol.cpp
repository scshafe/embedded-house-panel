#include "bhp/media_lease_protocol/media_lease_protocol.h"
#include <array>
#include <nlohmann/json.hpp>
#include <cstring>
#include <set>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
namespace bhp::media_lease_protocol
{
namespace
{
using Json = nlohmann::json;

Error wire_error(const char *operation, std::string detail = {})
{
	Error error{"MEDIA_LEASE.WIRE_INVALID", "Invalid media lease frame", {{"operation", operation}}};
	if (!detail.empty())
		error.context.emplace("detail", std::move(detail));
	return error;
}

bool hex(std::string_view text, std::size_t size)
{
	if (text.size() != size)
		return false;
	for (const char c : text)
		if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
			return false;
	return true;
}

bool opaque_id(std::string_view text)
{
	if (text.size() < 22 || text.size() > 128)
		return false;
	for (const char c : text)
		if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '-'))
			return false;
	return true;
}

bool printable(std::string_view text, std::size_t maximum)
{
	if (text.empty() || text.size() > maximum)
		return false;
	for (const char c : text)
		if (c < 0x21 || c > 0x7e)
			return false;
	return true;
}

bool valid_binding(const PresentationBinding &binding)
{
	return binding.lifecycle_protocol_version > 0 && hex(binding.supervisor_session_id, 32) &&
	       binding.lifecycle_event_sequence > 0 && printable(binding.lifecycle_snapshot_id, 128) &&
	       printable(binding.ui_connection_challenge, 128);
}

const std::array<std::pair<Purpose, std::string_view>, 3> kPurposes{{
	{Purpose::VoiceTake, "voice_take"},
	{Purpose::CommandRecognition, "command_recognition"},
	{Purpose::WishRecording, "wish_recording"},
}};

bool exact_keys(const Json &object, std::initializer_list<const char *> keys)
{
	if (!object.is_object() || object.size() != keys.size())
		return false;
	for (const char *key : keys)
		if (!object.contains(key))
			return false;
	return true;
}

bool read_unsigned(const Json &object, const char *key, std::uint64_t &out)
{
	const auto &value = object.at(key);
	if (!value.is_number_unsigned())
		return false;
	out = value.get<std::uint64_t>();
	return out <= 9007199254740991ULL;
}

bool read_string(const Json &object, const char *key, std::string &out)
{
	const auto &value = object.at(key);
	if (!value.is_string())
		return false;
	out = value.get<std::string>();
	return true;
}

Json binding_json(const PresentationBinding &binding)
{
	return {{"lifecycle_protocol_version", binding.lifecycle_protocol_version},
		{"supervisor_session_id", binding.supervisor_session_id},
		{"lifecycle_event_sequence", binding.lifecycle_event_sequence},
		{"lifecycle_snapshot_id", binding.lifecycle_snapshot_id},
		{"ui_connection_challenge", binding.ui_connection_challenge},
		{"committed_render_generation", binding.committed_render_generation}};
}

bool read_binding(const Json &object, PresentationBinding &binding)
{
	std::uint64_t version = 0;
	if (!exact_keys(object, {"lifecycle_protocol_version", "supervisor_session_id", "lifecycle_event_sequence",
				 "lifecycle_snapshot_id", "ui_connection_challenge", "committed_render_generation"}) ||
	    !read_unsigned(object, "lifecycle_protocol_version", version) || version > 0xffffffffULL ||
	    !read_string(object, "supervisor_session_id", binding.supervisor_session_id) ||
	    !read_unsigned(object, "lifecycle_event_sequence", binding.lifecycle_event_sequence) ||
	    !read_string(object, "lifecycle_snapshot_id", binding.lifecycle_snapshot_id) ||
	    !read_string(object, "ui_connection_challenge", binding.ui_connection_challenge) ||
	    !read_unsigned(object, "committed_render_generation", binding.committed_render_generation))
		return false;
	binding.lifecycle_protocol_version = static_cast<std::uint32_t>(version);
	return valid_binding(binding);
}

Result<Json> parse_frame(std::string_view frame, const char *operation)
{
	if (frame.size() > media_window_protocol::kMaximumFrameBytes || frame.empty() || frame.back() != '\n' ||
	    frame.find('\n') != frame.size() - 1)
		return Result<Json>::failure(wire_error(operation, "framing"));
	std::vector<std::set<std::string>> keys;
	bool duplicate = false;
	auto callback = [&](int, Json::parse_event_t event, Json &parsed) {
		if (event == Json::parse_event_t::object_start)
			keys.emplace_back();
		else if (event == Json::parse_event_t::object_end && !keys.empty())
			keys.pop_back();
		else if (event == Json::parse_event_t::key && !keys.empty() &&
			 !keys.back().insert(parsed.get<std::string>()).second)
			duplicate = true;
		return true;
	};
	Json value = Json::parse(frame.substr(0, frame.size() - 1), callback, false);
	std::uint64_t version = 0;
	if (value.is_discarded() || duplicate || !value.is_object() || !value.contains("type") ||
	    !value["type"].is_string() || !value.contains("version") || !read_unsigned(value, "version", version) ||
	    version != kProtocolVersion)
		return Result<Json>::failure(wire_error(operation, duplicate ? "duplicate_key" : "envelope"));
	return Result<Json>::success(std::move(value));
}

Result<std::string> finish(Json object, const char *type)
{
	object["type"] = type;
	object["version"] = kProtocolVersion;
	std::string frame = object.dump() + "\n";
	if (frame.size() > media_window_protocol::kMaximumFrameBytes)
		return Result<std::string>::failure(wire_error("encode", "frame_bound"));
	return Result<std::string>::success(std::move(frame));
}

bool valid_take_id(std::string_view text)
{
	return text.substr(0, 5) == "take-" && hex(text.substr(5), 64);
}

bool valid(const Ended &ended)
{
	if (!opaque_id(ended.lease_id))
		return false;
	if (ended.outcome == Outcome::Committed)
		return valid_take_id(ended.take_id) && (ended.stop_reason == "requested" || ended.stop_reason == "maximum-duration") &&
		       ended.frames > 0 && ended.error_code.empty();
	return printable(ended.error_code, 96) && ended.take_id.empty() && ended.stop_reason.empty() && ended.frames == 0;
}

bool valid(const Level &level)
{
	if (!opaque_id(level.lease_id) || level.peak_decibels_tenths.empty() ||
	    level.peak_decibels_tenths.size() > kMaximumChannels)
		return false;
	for (const auto value : level.peak_decibels_tenths)
		if (value < -1200 || value > 0)
			return false;
	return true;
}
} // namespace

std::string_view to_string(const Purpose purpose)
{
	for (const auto &[value, name] : kPurposes)
		if (value == purpose)
			return name;
	return "voice_take";
}

Result<std::string> encode(const UiRequest &request)
{
	if (const auto *arm = std::get_if<ArmRequest>(&request)) {
		if (!opaque_id(arm->request_id) || !opaque_id(arm->touch_observation_id) ||
		    !valid_binding(arm->presentation_binding) || !printable(arm->operation_ref, 128))
			return Result<std::string>::failure(wire_error("encode_arm"));
		return finish({{"request_id", arm->request_id},
			       {"purpose", to_string(arm->purpose)},
			       {"touch_observation_id", arm->touch_observation_id},
			       {"presentation_binding", binding_json(arm->presentation_binding)},
			       {"operation_ref", arm->operation_ref}},
			      "arm");
	}
	if (const auto *discard = std::get_if<DiscardRequest>(&request)) {
		if (!valid_take_id(discard->take_id))
			return Result<std::string>::failure(wire_error("encode_discard"));
		return finish({{"take_id", discard->take_id}}, "discard");
	}
	if (const auto *hand_off = std::get_if<HandOffRequest>(&request)) {
		if (!valid_take_id(hand_off->take_id))
			return Result<std::string>::failure(wire_error("encode_hand_off"));
		return finish({{"take_id", hand_off->take_id}}, "hand_off");
	}
	const bool start = std::holds_alternative<StartRequest>(request);
	const auto &lease = start ? std::get<StartRequest>(request).lease_id : std::get<StopRequest>(request).lease_id;
	if (!opaque_id(lease))
		return Result<std::string>::failure(wire_error("encode_lease"));
	return finish({{"lease_id", lease}}, start ? "start" : "stop");
}

Result<std::string> encode(const BrokerEvent &event)
{
	return std::visit(
		[](const auto &value) -> Result<std::string> {
			using Type = std::decay_t<decltype(value)>;
			if constexpr (std::is_same_v<Type, Armed>) {
				if (!opaque_id(value.request_id) || !opaque_id(value.lease_id))
					return Result<std::string>::failure(wire_error("encode_armed"));
				return finish({{"request_id", value.request_id}, {"lease_id", value.lease_id}}, "armed");
			} else if constexpr (std::is_same_v<Type, Refused>) {
				if ((!opaque_id(value.request_id) && !valid_take_id(value.request_id)) ||
				    !printable(value.error_code, 96))
					return Result<std::string>::failure(wire_error("encode_refused"));
				return finish({{"request_id", value.request_id}, {"error_code", value.error_code}}, "refused");
			} else if constexpr (std::is_same_v<Type, Started>) {
				if (!opaque_id(value.lease_id))
					return Result<std::string>::failure(wire_error("encode_started"));
				return finish({{"lease_id", value.lease_id}}, "started");
			} else if constexpr (std::is_same_v<Type, Discarded>) {
				if (!valid_take_id(value.take_id))
					return Result<std::string>::failure(wire_error("encode_discarded"));
				return finish({{"take_id", value.take_id}}, "discarded");
			} else if constexpr (std::is_same_v<Type, HandedOff>) {
				if (!valid_take_id(value.take_id))
					return Result<std::string>::failure(wire_error("encode_handed_off"));
				return finish({{"take_id", value.take_id}}, "handed_off");
			} else if constexpr (std::is_same_v<Type, Level>) {
				if (!valid(value))
					return Result<std::string>::failure(wire_error("encode_level"));
				return finish({{"lease_id", value.lease_id},
					       {"elapsed_milliseconds", value.elapsed_milliseconds},
					       {"peak_decibels_tenths", value.peak_decibels_tenths}},
					      "level");
			} else {
				if (!valid(value))
					return Result<std::string>::failure(wire_error("encode_ended"));
				const bool committed = value.outcome == Outcome::Committed;
				return finish({{"lease_id", value.lease_id},
					       {"outcome", committed ? "committed" : "failed"},
					       {"take_id", committed ? Json(value.take_id) : Json(nullptr)},
					       {"stop_reason", committed ? Json(value.stop_reason) : Json(nullptr)},
					       {"frames", value.frames},
					       {"error_code", committed ? Json(nullptr) : Json(value.error_code)}},
					      "ended");
			}
		},
		event);
}

Result<UiRequest> decode_ui_request(std::string_view frame)
{
	using Output = Result<UiRequest>;
	auto parsed = parse_frame(frame, "decode_ui_request");
	if (!parsed)
		return Output::failure(parsed.error());
	const Json &value = parsed.value();
	const auto type = value["type"].get<std::string>();
	if (type == "arm") {
		ArmRequest arm;
		std::string purpose;
		if (!exact_keys(value, {"type", "version", "request_id", "purpose", "touch_observation_id",
					"presentation_binding", "operation_ref"}) ||
		    !read_string(value, "request_id", arm.request_id) || !read_string(value, "purpose", purpose) ||
		    !read_string(value, "touch_observation_id", arm.touch_observation_id) ||
		    !read_binding(value["presentation_binding"], arm.presentation_binding) ||
		    !read_string(value, "operation_ref", arm.operation_ref))
			return Output::failure(wire_error("decode_arm"));
		bool known = false;
		for (const auto &[candidate, name] : kPurposes)
			if (name == purpose) {
				arm.purpose = candidate;
				known = true;
			}
		if (!known || !encode(UiRequest(arm)))
			return Output::failure(wire_error("decode_arm", "values"));
		return Output::success(std::move(arm));
	}
	if (type == "discard") {
		std::string take;
		if (!exact_keys(value, {"type", "version", "take_id"}) || !read_string(value, "take_id", take) ||
		    !valid_take_id(take))
			return Output::failure(wire_error("decode_discard"));
		return Output::success(DiscardRequest{take});
	}
	if (type == "hand_off") {
		std::string take;
		if (!exact_keys(value, {"type", "version", "take_id"}) || !read_string(value, "take_id", take) ||
		    !valid_take_id(take))
			return Output::failure(wire_error("decode_hand_off"));
		return Output::success(HandOffRequest{take});
	}
	if (type == "start" || type == "stop") {
		std::string lease;
		if (!exact_keys(value, {"type", "version", "lease_id"}) || !read_string(value, "lease_id", lease) ||
		    !opaque_id(lease))
			return Output::failure(wire_error("decode_lease"));
		return type == "start" ? Output::success(StartRequest{lease}) : Output::success(StopRequest{lease});
	}
	return Output::failure(wire_error("decode_ui_request", "type"));
}

Result<BrokerEvent> decode_broker_event(std::string_view frame)
{
	using Output = Result<BrokerEvent>;
	auto parsed = parse_frame(frame, "decode_broker_event");
	if (!parsed)
		return Output::failure(parsed.error());
	const Json &value = parsed.value();
	const auto type = value["type"].get<std::string>();
	auto checked = [](BrokerEvent event) {
		return encode(event) ? Output::success(std::move(event)) : Output::failure(wire_error("decode_broker_event", "values"));
	};
	if (type == "armed") {
		Armed armed;
		if (!exact_keys(value, {"type", "version", "request_id", "lease_id"}) ||
		    !read_string(value, "request_id", armed.request_id) || !read_string(value, "lease_id", armed.lease_id))
			return Output::failure(wire_error("decode_armed"));
		return checked(armed);
	}
	if (type == "refused") {
		Refused refused;
		if (!exact_keys(value, {"type", "version", "request_id", "error_code"}) ||
		    !read_string(value, "request_id", refused.request_id) ||
		    !read_string(value, "error_code", refused.error_code))
			return Output::failure(wire_error("decode_refused"));
		return checked(refused);
	}
	if (type == "started") {
		Started started;
		if (!exact_keys(value, {"type", "version", "lease_id"}) || !read_string(value, "lease_id", started.lease_id))
			return Output::failure(wire_error("decode_started"));
		return checked(started);
	}
	if (type == "discarded") {
		Discarded discarded;
		if (!exact_keys(value, {"type", "version", "take_id"}) || !read_string(value, "take_id", discarded.take_id))
			return Output::failure(wire_error("decode_discarded"));
		return checked(discarded);
	}
	if (type == "handed_off") {
		HandedOff handed_off;
		if (!exact_keys(value, {"type", "version", "take_id"}) ||
		    !read_string(value, "take_id", handed_off.take_id))
			return Output::failure(wire_error("decode_handed_off"));
		return checked(handed_off);
	}
	if (type == "level") {
		Level level;
		if (!exact_keys(value, {"type", "version", "lease_id", "elapsed_milliseconds", "peak_decibels_tenths"}) ||
		    !read_string(value, "lease_id", level.lease_id) ||
		    !read_unsigned(value, "elapsed_milliseconds", level.elapsed_milliseconds) ||
		    !value["peak_decibels_tenths"].is_array())
			return Output::failure(wire_error("decode_level"));
		for (const auto &peak : value["peak_decibels_tenths"]) {
			if (!peak.is_number_integer())
				return Output::failure(wire_error("decode_level", "peak"));
			const auto number = peak.get<std::int64_t>();
			if (number < -1200 || number > 0)
				return Output::failure(wire_error("decode_level", "peak"));
			level.peak_decibels_tenths.push_back(static_cast<std::int32_t>(number));
		}
		return checked(level);
	}
	if (type == "ended") {
		Ended ended;
		std::string outcome;
		if (!exact_keys(value, {"type", "version", "lease_id", "outcome", "take_id", "stop_reason", "frames",
					"error_code"}) ||
		    !read_string(value, "lease_id", ended.lease_id) || !read_string(value, "outcome", outcome) ||
		    !read_unsigned(value, "frames", ended.frames))
			return Output::failure(wire_error("decode_ended"));
		if (outcome == "committed" && value["take_id"].is_string() && value["stop_reason"].is_string() &&
		    value["error_code"].is_null()) {
			ended.outcome = Outcome::Committed;
			ended.take_id = value["take_id"].get<std::string>();
			ended.stop_reason = value["stop_reason"].get<std::string>();
		} else if (outcome == "failed" && value["take_id"].is_null() && value["stop_reason"].is_null() &&
			   value["error_code"].is_string()) {
			ended.outcome = Outcome::Failed;
			ended.error_code = value["error_code"].get<std::string>();
		} else {
			return Output::failure(wire_error("decode_ended", "outcome"));
		}
		return checked(ended);
	}
	return Output::failure(wire_error("decode_broker_event", "type"));
}

Result<std::unique_ptr<MediaLeaseClient>> MediaLeaseClient::connect(const std::string &path,
								    const std::uint32_t broker_uid)
{
	using Output = Result<std::unique_ptr<MediaLeaseClient>>;
	if (path.empty() || path[0] != '/' || path.size() >= sizeof(sockaddr_un::sun_path))
		return Output::failure({"MEDIA_LEASE.SOCKET_INVALID", "Broker socket path is invalid", {{"operation", "connect"}}});
	const int descriptor = ::socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
	sockaddr_un address{};
	address.sun_family = AF_UNIX;
	std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
	if (descriptor < 0 || ::connect(descriptor, reinterpret_cast<sockaddr *>(&address), sizeof address) != 0) {
		if (descriptor >= 0)
			::close(descriptor);
		return Output::failure({"MEDIA_LEASE.BROKER_UNAVAILABLE", "Broker socket did not accept", {{"operation", "connect"}}});
	}
	return for_connected_peer(descriptor, broker_uid);
}

Result<std::unique_ptr<MediaLeaseClient>> MediaLeaseClient::for_connected_peer(const int descriptor,
									       const std::uint32_t broker_uid)
{
	using Output = Result<std::unique_ptr<MediaLeaseClient>>;
	const auto uid = media_window_protocol::peer_uid(descriptor);
	if (!uid || *uid != broker_uid) {
		::close(descriptor);
		return Output::failure({"MEDIA_LEASE.PEER_REJECTED", "Broker socket peer is not the broker UID", {{"operation", "connect"}}});
	}
	return Output::success(std::unique_ptr<MediaLeaseClient>(new MediaLeaseClient(descriptor)));
}

void MediaLeaseClient::close() noexcept
{
	if (channel_)
		channel_->close();
}

Result<Done> MediaLeaseClient::send(const UiRequest &request)
{
	auto frame = encode(request);
	if (!frame)
		return Result<Done>::failure(frame.error());
	if (!connected())
		return Result<Done>::failure({"MEDIA_LEASE.BROKER_UNAVAILABLE", "Broker is not connected", {{"operation", "send"}}});
	return channel_->send(frame.value());
}

Result<std::vector<BrokerEvent>> MediaLeaseClient::poll()
{
	using Output = Result<std::vector<BrokerEvent>>;
	std::vector<BrokerEvent> events;
	for (int frames = 0; frames < 16; ++frames) {
		if (!connected())
			return Output::failure({"MEDIA_LEASE.BROKER_UNAVAILABLE", "Broker is not connected", {{"operation", "poll"}}});
		auto received = channel_->receive();
		if (!received)
			return Output::failure(received.error());
		if (!received.value())
			break;
		auto event = decode_broker_event(*received.value());
		if (!event) {
			close();
			return Output::failure(event.error());
		}
		events.push_back(std::move(event.value()));
	}
	return Output::success(std::move(events));
}
} // namespace bhp::media_lease_protocol

