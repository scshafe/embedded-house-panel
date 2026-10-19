#include "bhp/media_window_protocol/media_window_protocol.h"
#include <array>
#include <cerrno>
#include <nlohmann/json.hpp>
#include <set>
#include <cstring>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <vector>
namespace bhp::media_window_protocol
{
namespace
{
using Json = nlohmann::json;

Error wire_error(const char *operation, std::string detail = {})
{
	Error error{"MEDIA_WINDOW.WIRE_INVALID", "Invalid media window frame", {{"operation", operation}}};
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

bool sha256_identifier(std::string_view text)
{
	return text.substr(0, 7) == "sha256:" && hex(text.substr(7), 64);
}

/** Opaque IDs: 22 to 128 of [A-Za-z0-9_-]. */
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

Json binding_json(const PresentationBinding &binding)
{
	return {{"lifecycle_protocol_version", binding.lifecycle_protocol_version},
		{"supervisor_session_id", binding.supervisor_session_id},
		{"lifecycle_event_sequence", binding.lifecycle_event_sequence},
		{"lifecycle_snapshot_id", binding.lifecycle_snapshot_id},
		{"ui_connection_challenge", binding.ui_connection_challenge},
		{"committed_render_generation", binding.committed_render_generation}};
}

/** Exactly these keys; the caller then reads each with the right type. */
bool exact_keys(const Json &object, std::initializer_list<const char *> keys)
{
	if (!object.is_object() || object.size() != keys.size())
		return false;
	for (const char *key : keys)
		if (!object.contains(key))
			return false;
	return true;
}

bool unsigned_field(const Json &object, const char *key, std::uint64_t &out)
{
	const auto &value = object.at(key);
	if (!value.is_number_unsigned())
		return false;
	out = value.get<std::uint64_t>();
	return out <= 9007199254740991ULL;
}

bool string_field(const Json &object, const char *key, std::string &out)
{
	const auto &value = object.at(key);
	if (!value.is_string())
		return false;
	out = value.get<std::string>();
	return true;
}

bool read_binding(const Json &object, PresentationBinding &binding)
{
	std::uint64_t version = 0;
	if (!exact_keys(object, {"lifecycle_protocol_version", "supervisor_session_id", "lifecycle_event_sequence",
				 "lifecycle_snapshot_id", "ui_connection_challenge", "committed_render_generation"}) ||
	    !unsigned_field(object, "lifecycle_protocol_version", version) || version > 0xffffffffULL ||
	    !string_field(object, "supervisor_session_id", binding.supervisor_session_id) ||
	    !unsigned_field(object, "lifecycle_event_sequence", binding.lifecycle_event_sequence) ||
	    !string_field(object, "lifecycle_snapshot_id", binding.lifecycle_snapshot_id) ||
	    !string_field(object, "ui_connection_challenge", binding.ui_connection_challenge) ||
	    !unsigned_field(object, "committed_render_generation", binding.committed_render_generation))
		return false;
	binding.lifecycle_protocol_version = static_cast<std::uint32_t>(version);
	return valid_binding(binding);
}

/** Parses one frame, refusing duplicate keys, a missing newline and oversize input. */
Result<Json> parse_frame(std::string_view frame, const char *operation)
{
	if (frame.size() > kMaximumFrameBytes || frame.empty() || frame.back() != '\n' ||
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
	if (value.is_discarded() || duplicate || !value.is_object())
		return Result<Json>::failure(wire_error(operation, duplicate ? "duplicate_key" : "json"));
	std::uint64_t version = 0;
	if (!value.contains("version") || !unsigned_field(value, "version", version) || version != kProtocolVersion)
		return Result<Json>::failure(wire_error(operation, "version"));
	return Result<Json>::success(std::move(value));
}

Result<std::string> finish(const Json &object)
{
	std::string frame = object.dump() + "\n";
	if (frame.size() > kMaximumFrameBytes)
		return Result<std::string>::failure(wire_error("encode", "frame_bound"));
	return Result<std::string>::success(std::move(frame));
}

const std::array<std::pair<WindowReason, std::string_view>, 5> kReasons{{
	{WindowReason::DayPresent, "day_present"},
	{WindowReason::Absence, "absence"},
	{WindowReason::Night, "night"},
	{WindowReason::SuspendPreparation, "suspend_preparation"},
	{WindowReason::SupervisorStopping, "supervisor_stopping"},
}};
} // namespace

std::string_view to_string(const WindowReason reason)
{
	for (const auto &[value, name] : kReasons)
		if (value == reason)
			return name;
	return "absence";
}

Result<std::string> encode(const WindowMessage &message)
{
	if (!hex(message.supervisor_session_id, 32) || message.sequence == 0 ||
	    message.open != (message.reason == WindowReason::DayPresent))
		return Result<std::string>::failure(wire_error("encode_window"));
	return finish({{"type", "window"},
		       {"version", kProtocolVersion},
		       {"supervisor_session_id", message.supervisor_session_id},
		       {"sequence", message.sequence},
		       {"open", message.open},
		       {"reason", to_string(message.reason)}});
}

Result<std::string> encode(const ClaimResult &message)
{
	if (!hex(message.supervisor_session_id, 32) || message.sequence == 0 || !opaque_id(message.request_id) ||
	    message.envelope.has_value() == !message.error_code.empty())
		return Result<std::string>::failure(wire_error("encode_claim_result"));
	Json envelope = nullptr;
	if (message.envelope) {
		const auto &e = *message.envelope;
		if (!opaque_id(e.touch_observation_id) || !sha256_identifier(e.canonical_operation_digest) ||
		    !valid_binding(e.presentation_binding))
			return Result<std::string>::failure(wire_error("encode_claim_result", "envelope"));
		envelope = {{"touch_observation_id", e.touch_observation_id},
			    {"observation_sequence", e.observation_sequence},
			    {"canonical_operation_digest", e.canonical_operation_digest},
			    {"presentation_binding", binding_json(e.presentation_binding)},
			    {"contact_duration_milliseconds", e.contact_duration_milliseconds}};
	} else if (!printable(message.error_code, 96)) {
		return Result<std::string>::failure(wire_error("encode_claim_result", "error_code"));
	}
	return finish({{"type", "claim_result"},
		       {"version", kProtocolVersion},
		       {"supervisor_session_id", message.supervisor_session_id},
		       {"sequence", message.sequence},
		       {"request_id", message.request_id},
		       {"envelope", envelope},
		       {"error_code", message.envelope ? Json(nullptr) : Json(message.error_code)}});
}

Result<std::string> encode(const ClaimRequest &message)
{
	if (!opaque_id(message.request_id) || !opaque_id(message.touch_observation_id) ||
	    !sha256_identifier(message.canonical_operation_digest) || !valid_binding(message.presentation_binding))
		return Result<std::string>::failure(wire_error("encode_claim"));
	return finish({{"type", "claim"},
		       {"version", kProtocolVersion},
		       {"request_id", message.request_id},
		       {"touch_observation_id", message.touch_observation_id},
		       {"canonical_operation_digest", message.canonical_operation_digest},
		       {"presentation_binding", binding_json(message.presentation_binding)}});
}

Result<SupervisorMessage> decode_supervisor_message(std::string_view frame)
{
	using Output = Result<SupervisorMessage>;
	auto parsed = parse_frame(frame, "decode_supervisor_message");
	if (!parsed)
		return Output::failure(parsed.error());
	const Json &value = parsed.value();
	const auto type = value.contains("type") && value["type"].is_string() ? value["type"].get<std::string>() : "";
	if (type == "window") {
		WindowMessage message;
		std::string reason;
		if (!exact_keys(value, {"type", "version", "supervisor_session_id", "sequence", "open", "reason"}) ||
		    !string_field(value, "supervisor_session_id", message.supervisor_session_id) ||
		    !unsigned_field(value, "sequence", message.sequence) || !value["open"].is_boolean() ||
		    !string_field(value, "reason", reason))
			return Output::failure(wire_error("decode_window"));
		message.open = value["open"].get<bool>();
		bool known = false;
		for (const auto &[candidate, name] : kReasons)
			if (name == reason) {
				message.reason = candidate;
				known = true;
			}
		if (!known || !encode(message))
			return Output::failure(wire_error("decode_window", "values"));
		return Output::success(message);
	}
	if (type == "claim_result") {
		ClaimResult message;
		if (!exact_keys(value, {"type", "version", "supervisor_session_id", "sequence", "request_id", "envelope",
					"error_code"}) ||
		    !string_field(value, "supervisor_session_id", message.supervisor_session_id) ||
		    !unsigned_field(value, "sequence", message.sequence) ||
		    !string_field(value, "request_id", message.request_id))
			return Output::failure(wire_error("decode_claim_result"));
		const Json &envelope = value["envelope"];
		const Json &error_code = value["error_code"];
		if (envelope.is_object() && error_code.is_null()) {
			ClaimEnvelope e;
			if (!exact_keys(envelope, {"touch_observation_id", "observation_sequence", "canonical_operation_digest",
						   "presentation_binding", "contact_duration_milliseconds"}) ||
			    !string_field(envelope, "touch_observation_id", e.touch_observation_id) ||
			    !unsigned_field(envelope, "observation_sequence", e.observation_sequence) ||
			    !string_field(envelope, "canonical_operation_digest", e.canonical_operation_digest) ||
			    !read_binding(envelope["presentation_binding"], e.presentation_binding) ||
			    !unsigned_field(envelope, "contact_duration_milliseconds", e.contact_duration_milliseconds))
				return Output::failure(wire_error("decode_claim_result", "envelope"));
			message.envelope = std::move(e);
		} else if (envelope.is_null() && error_code.is_string()) {
			message.error_code = error_code.get<std::string>();
		} else {
			return Output::failure(wire_error("decode_claim_result", "outcome"));
		}
		if (!encode(message))
			return Output::failure(wire_error("decode_claim_result", "values"));
		return Output::success(std::move(message));
	}
	return Output::failure(wire_error("decode_supervisor_message", "type"));
}

Result<ClaimRequest> decode_claim_request(std::string_view frame)
{
	auto parsed = parse_frame(frame, "decode_claim_request");
	if (!parsed)
		return Result<ClaimRequest>::failure(parsed.error());
	const Json &value = parsed.value();
	ClaimRequest message;
	std::string type;
	if (!exact_keys(value, {"type", "version", "request_id", "touch_observation_id", "canonical_operation_digest",
				"presentation_binding"}) ||
	    !string_field(value, "type", type) || type != "claim" ||
	    !string_field(value, "request_id", message.request_id) ||
	    !string_field(value, "touch_observation_id", message.touch_observation_id) ||
	    !string_field(value, "canonical_operation_digest", message.canonical_operation_digest) ||
	    !read_binding(value["presentation_binding"], message.presentation_binding) || !encode(message))
		return Result<ClaimRequest>::failure(wire_error("decode_claim_request"));
	return Result<ClaimRequest>::success(std::move(message));
}

FrameChannel::~FrameChannel() { close(); }

void FrameChannel::close() noexcept
{
	if (descriptor_ >= 0)
		::close(descriptor_);
	descriptor_ = -1;
	buffered_.clear();
}

Result<std::optional<std::string>> FrameChannel::receive()
{
	using Output = Result<std::optional<std::string>>;
	auto closed = [this](const char *detail) {
		close();
		return Output::failure({"MEDIA_WINDOW.CHANNEL_CLOSED", "Media window channel closed",
					{{"operation", "frame_channel.receive"}, {"detail", detail}}});
	};
	if (descriptor_ < 0)
		return closed("not_open");
	if (buffered_.find('\n') == std::string::npos) {
		std::array<char, kMaximumFrameBytes> bytes{};
		const auto room = kMaximumFrameBytes - buffered_.size();
		const auto count = ::recv(descriptor_, bytes.data(), room, MSG_DONTWAIT);
		if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
			return Output::success(std::nullopt);
		if (count <= 0)
			return closed("peer_closed");
		buffered_.append(bytes.data(), static_cast<std::size_t>(count));
	}
	const auto end = buffered_.find('\n');
	if (end == std::string::npos) {
		if (buffered_.size() >= kMaximumFrameBytes)
			return closed("frame_bound");
		return Output::success(std::nullopt);
	}
	std::string frame = buffered_.substr(0, end + 1);
	buffered_.erase(0, end + 1);
	return Output::success(std::move(frame));
}

Result<Done> FrameChannel::send(const std::string &frame)
{
	if (descriptor_ >= 0) {
		const auto sent = ::send(descriptor_, frame.data(), frame.size(), MSG_DONTWAIT | MSG_NOSIGNAL);
		if (sent == static_cast<ssize_t>(frame.size()))
			return Result<Done>::success({});
	}
	close();
	return Result<Done>::failure({"MEDIA_WINDOW.CHANNEL_CLOSED", "Media window frame was not sent whole",
				      {{"operation", "frame_channel.send"}}});
}

std::optional<std::uint32_t> peer_uid(const int descriptor)
{
	struct ucred credentials {};
	socklen_t size = sizeof credentials;
	if (::getsockopt(descriptor, SOL_SOCKET, SO_PEERCRED, &credentials, &size) != 0 || size != sizeof credentials)
		return std::nullopt;
	return credentials.uid;
}

namespace
{
bool trusted_socket(const std::string &path, struct stat &info)
{
	if (path.empty() || path[0] != '/' || path.size() >= sizeof(sockaddr_un::sun_path) ||
	    path.find("//") != std::string::npos || path.find("/../") != std::string::npos ||
	    path.find("/./") != std::string::npos || path.back() == '/')
		return false;
	for (std::size_t end = 1; end != std::string::npos; end = path.find('/', end + 1)) {
		if (end == path.size())
			break;
		const auto parent = end == 1 ? std::string("/") : path.substr(0, end);
		struct stat directory {};
		if (::lstat(parent.c_str(), &directory) != 0 || !S_ISDIR(directory.st_mode) || directory.st_uid != 0 ||
		    (directory.st_mode & 0022) != 0)
			return false;
	}
	return ::lstat(path.c_str(), &info) == 0 && S_ISSOCK(info.st_mode) && info.st_uid == 0 &&
	       (info.st_mode & 0007) == 0;
}

Error client_error(const char *code, const char *operation)
{
	return {code, "Media window client operation failed", {{"operation", operation}}};
}
} // namespace

Result<std::unique_ptr<MediaWindowClient>> MediaWindowClient::connect(const std::string &path,
								      const std::uint32_t supervisor_uid)
{
	using Output = Result<std::unique_ptr<MediaWindowClient>>;
	struct stat before {}, after {};
	if (!trusted_socket(path, before))
		return Output::failure(client_error("MEDIA_WINDOW.SOCKET_UNTRUSTED", "connect.path"));
	const int descriptor = ::socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
	if (descriptor < 0)
		return Output::failure(client_error("MEDIA_WINDOW.SOCKET_UNAVAILABLE", "connect.socket"));
	sockaddr_un address{};
	address.sun_family = AF_UNIX;
	std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
	if (::connect(descriptor, reinterpret_cast<sockaddr *>(&address), sizeof address) != 0 ||
	    !trusted_socket(path, after) || before.st_dev != after.st_dev || before.st_ino != after.st_ino) {
		::close(descriptor);
		return Output::failure(client_error("MEDIA_WINDOW.SOCKET_UNTRUSTED", "connect.authenticate"));
	}
	return for_connected_peer(descriptor, supervisor_uid);
}

Result<std::unique_ptr<MediaWindowClient>> MediaWindowClient::for_connected_peer(const int descriptor,
									 const std::uint32_t supervisor_uid)
{
	using Output = Result<std::unique_ptr<MediaWindowClient>>;
	const auto uid = peer_uid(descriptor);
	if (!uid || *uid != supervisor_uid) {
		::close(descriptor);
		return Output::failure(client_error("MEDIA_WINDOW.PEER_REJECTED", "connect.peer_uid"));
	}
	return Output::success(std::unique_ptr<MediaWindowClient>(new MediaWindowClient(descriptor)));
}

void MediaWindowClient::close() noexcept
{
	if (channel_)
		channel_->close();
	open_ = false;
}

Result<Done> MediaWindowClient::claim(const ClaimRequest &request)
{
	if (!window_open())
		return Result<Done>::failure(client_error("MEDIA_WINDOW.CLOSED", "claim"));
	auto frame = encode(request);
	if (!frame)
		return Result<Done>::failure(frame.error());
	auto sent = channel_->send(frame.value());
	if (!sent)
		close();
	return sent;
}

Result<MediaWindowClient::Update> MediaWindowClient::poll()
{
	Update update;
	const bool was_open = window_open();
	auto fail = [&](Error error) {
		close();
		return Result<Update>::failure(std::move(error));
	};
	for (int frames = 0; frames < 4; ++frames) {
		if (!connected())
			return fail(client_error("MEDIA_WINDOW.CHANNEL_CLOSED", "poll"));
		auto received = channel_->receive();
		if (!received)
			return fail(received.error());
		if (!received.value())
			break;
		auto message = decode_supervisor_message(*received.value());
		if (!message)
			return fail(message.error());
		const auto &[session, sequence] = std::visit(
			[](const auto &value) { return std::pair<std::string, std::uint64_t>(value.supervisor_session_id, value.sequence); },
			message.value());
		if (session_.empty() && sequence == 1)
			session_ = session;
		else if (session != session_ || sequence != sequence_ + 1)
			return fail(client_error("MEDIA_WINDOW.SEQUENCE_BROKEN", "poll"));
		sequence_ = sequence;
		if (const auto *window = std::get_if<WindowMessage>(&message.value())) {
			open_ = window->open;
			reason_ = window->reason;
		} else {
			update.claim_results.push_back(std::get<ClaimResult>(message.value()));
		}
	}
	update.window_changed = was_open != window_open();
	return Result<Update>::success(std::move(update));
}
} // namespace bhp::media_window_protocol

