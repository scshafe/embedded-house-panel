#include "music_control/music_protocol.h"
#include "bhp/media_window_protocol/media_window_protocol.h"
#include <algorithm>
#include <cstring>
#include <nlohmann/json.hpp>
#include <regex>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace music_control::protocol
{
namespace
{
using Json = nlohmann::json;

template <typename Value> Result<Value> fail(const std::string &where)
{
	return Result<Value>::failure({"MUSIC.PROTOCOL_INVALID", "The music-control frame is not valid", {{"where", where}}, false});
}

bool printable(const std::string &text, std::size_t maximum, bool allow_empty)
{
	if (text.size() > maximum || (!allow_empty && text.empty()))
		return false;
	return std::all_of(text.begin(), text.end(), [](unsigned char c) { return c >= 0x20 && c != 0x7f; });
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
	std::string text;
	try {
		text = value.dump(-1, ' ', false, Json::error_handler_t::strict) + "\n";
	} catch (const Json::exception &) {
		return fail<std::string>(std::string("encode_") + type + "_utf8");
	}
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

bool read(const Json &value, const char *key, bool &out)
{
	if (!value.contains(key) || !value[key].is_boolean())
		return false;
	out = value[key].get<bool>();
	return true;
}

bool valid(const State &state)
{
	const auto &output = state.output;
	if (!one_of(state.sign_in, {"signed_in", "signed_out"}) ||
	    !one_of(output.state, {"unknown", "missing", "restricted", "available", "active"}) ||
	    !printable(output.name, 128, false) || !code(state.error))
		return false;
	if (output.volume_percent && *output.volume_percent > 100)
		return false;
	if (state.playback && (output.state != "active" || !printable(state.playback->title, kMaximumTextBytes, true) ||
			       !printable(state.playback->artists, kMaximumTextBytes, true)))
		return false;
	return true;
}
} // namespace

std::string request_name(const UiRequest &request)
{
	static const char *names[] = {"status", "select_output", "play", "pause", "next", "volume"};
	return names[request.index()];
}

Result<std::string> encode(const UiRequest &request)
{
	if (const auto *volume = std::get_if<VolumeRequest>(&request)) {
		if (volume->percent > 100)
			return fail<std::string>("encode_volume");
		return finish({{"percent", volume->percent}}, "volume");
	}
	return finish(Json::object(), request_name(request).c_str());
}

Result<std::string> encode(const MusicEvent &event)
{
	if (const auto *refused = std::get_if<Refused>(&event)) {
		if (!one_of(refused->request, {"select_output", "play", "pause", "next", "volume"}) ||
		    refused->error_code.empty() || !code(refused->error_code))
			return fail<std::string>("encode_refused");
		return finish({{"request", refused->request}, {"error_code", refused->error_code}}, "refused");
	}
	const auto &state = std::get<State>(event);
	if (!valid(state))
		return fail<std::string>("encode_state");
	const auto &output = state.output;
	Json playback = nullptr;
	if (state.playback)
		playback = {{"is_playing", state.playback->is_playing},
			    {"title", state.playback->title},
			    {"artists", state.playback->artists}};
	return finish({{"sequence", state.sequence},
		       {"sign_in", state.sign_in},
		       {"output",
			{{"state", output.state},
			 {"name", output.name},
			 {"supports_volume", output.supports_volume},
			 {"volume_percent", output.volume_percent ? Json(*output.volume_percent) : Json(nullptr)}}},
		       {"playback", playback},
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
	if (type == "volume") {
		if (!exact_keys(value, {"type", "version", "percent"}) || !value["percent"].is_number_unsigned() ||
		    value["percent"].get<std::uint64_t>() > 100)
			return fail<UiRequest>("decode_volume");
		return Result<UiRequest>::success(VolumeRequest{value["percent"].get<std::uint32_t>()});
	}
	if (!exact_keys(value, {"type", "version"}))
		return fail<UiRequest>("decode_ui_request_keys");
	if (type == "status")
		return Result<UiRequest>::success(StatusRequest{});
	if (type == "select_output")
		return Result<UiRequest>::success(SelectOutputRequest{});
	if (type == "play")
		return Result<UiRequest>::success(PlayRequest{});
	if (type == "pause")
		return Result<UiRequest>::success(PauseRequest{});
	if (type == "next")
		return Result<UiRequest>::success(NextRequest{});
	return fail<UiRequest>("decode_ui_request_type");
}

Result<MusicEvent> decode_music_event(std::string_view frame)
{
	auto parsed = parse(frame, "decode_music_event");
	if (!parsed.has_value())
		return Result<MusicEvent>::failure(parsed.error());
	const Json &value = parsed.value();
	const std::string type = value["type"];
	if (type == "refused") {
		Refused refused;
		if (!exact_keys(value, {"type", "version", "request", "error_code"}) ||
		    !read(value, "request", refused.request) || !read(value, "error_code", refused.error_code) ||
		    !encode(MusicEvent(refused)).has_value())
			return fail<MusicEvent>("decode_refused");
		return Result<MusicEvent>::success(refused);
	}
	if (type != "state" || !exact_keys(value, {"type", "version", "sequence", "sign_in", "output", "playback", "error"}) ||
	    !value["sequence"].is_number_unsigned())
		return fail<MusicEvent>("decode_music_event_type");
	State state;
	state.sequence = value["sequence"].get<std::uint64_t>();
	const Json &output = value["output"];
	bool ok = read(value, "sign_in", state.sign_in) &&
		  exact_keys(output, {"state", "name", "supports_volume", "volume_percent"}) &&
		  read(output, "state", state.output.state) && read(output, "name", state.output.name) &&
		  read(output, "supports_volume", state.output.supports_volume);
	if (ok && !output["volume_percent"].is_null()) {
		ok = output["volume_percent"].is_number_unsigned() && output["volume_percent"].get<std::uint64_t>() <= 100;
		if (ok)
			state.output.volume_percent = output["volume_percent"].get<std::uint32_t>();
	}
	if (ok && !value["playback"].is_null()) {
		Playback playback;
		const Json &item = value["playback"];
		ok = exact_keys(item, {"is_playing", "title", "artists"}) && read(item, "is_playing", playback.is_playing) &&
		     read(item, "title", playback.title) && read(item, "artists", playback.artists);
		state.playback = playback;
	}
	if (ok && !value["error"].is_null())
		ok = read(value, "error", state.error) && !state.error.empty();
	if (!ok || !encode(MusicEvent(state)).has_value())
		return fail<MusicEvent>("decode_state");
	return Result<MusicEvent>::success(std::move(state));
}

struct MusicClient::Channel {
	explicit Channel(int descriptor) : frames(descriptor, kMaximumFrameBytes) {}
	bhp::media_window_protocol::FrameChannel frames;
};

MusicClient::MusicClient(int descriptor) : channel_(std::make_unique<Channel>(descriptor)) {}
MusicClient::~MusicClient() = default;

Result<std::unique_ptr<MusicClient>> MusicClient::connect(const std::string &path, std::uint32_t service_uid)
{
	using Output = Result<std::unique_ptr<MusicClient>>;
	if (path.empty() || path[0] != '/' || path.size() >= sizeof(sockaddr_un::sun_path))
		return Output::failure({"MUSIC.CONTROL_SOCKET_INVALID", "The music-control socket path is invalid", {}, false});
	const int descriptor = ::socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
	sockaddr_un address{};
	address.sun_family = AF_UNIX;
	std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
	if (descriptor < 0 || ::connect(descriptor, reinterpret_cast<sockaddr *>(&address), sizeof address) != 0) {
		if (descriptor >= 0)
			::close(descriptor);
		return Output::failure({"MUSIC.CONTROL_UNAVAILABLE", "The music-control socket did not accept", {}, true});
	}
	return for_connected_peer(descriptor, service_uid);
}

Result<std::unique_ptr<MusicClient>> MusicClient::for_connected_peer(int descriptor, std::uint32_t service_uid)
{
	using Output = Result<std::unique_ptr<MusicClient>>;
	const auto uid = bhp::media_window_protocol::peer_uid(descriptor);
	if (!uid || *uid != service_uid) {
		::close(descriptor);
		return Output::failure({"MUSIC.CONTROL_PEER_REJECTED", "The socket's peer is not the music-control UID", {}, false});
	}
	return Output::success(std::unique_ptr<MusicClient>(new MusicClient(descriptor)));
}

bool MusicClient::connected() const
{
	return channel_->frames.open();
}

Result<bool> MusicClient::send(const UiRequest &request)
{
	auto frame = encode(request);
	if (!frame.has_value())
		return Result<bool>::failure(frame.error());
	if (!channel_->frames.send(frame.value()))
		return Result<bool>::failure({"MUSIC.CONTROL_UNAVAILABLE", "The music-control service went away", {}, true});
	return Result<bool>::success(true);
}

Result<std::vector<MusicEvent>> MusicClient::poll()
{
	using Output = Result<std::vector<MusicEvent>>;
	std::vector<MusicEvent> events;
	for (int frames = 0; frames < 16; ++frames) {
		auto received = channel_->frames.receive();
		if (!received)
			return Output::failure({"MUSIC.CONTROL_UNAVAILABLE", "The music-control service went away", {}, true});
		if (!received.value())
			break;
		auto event = decode_music_event(*received.value());
		if (!event.has_value()) {
			channel_->frames.close();
			return Output::failure({"MUSIC.CONTROL_UNAVAILABLE", "The music-control service sent a malformed frame", {}, true});
		}
		events.push_back(std::move(event.value()));
	}
	return Output::success(std::move(events));
}
} // namespace music_control::protocol
