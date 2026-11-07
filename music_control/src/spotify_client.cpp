#include "music_control/spotify_client.h"
#include <algorithm>
#include <cctype>
#include <nlohmann/json.hpp>
namespace music_control
{
namespace
{
using Json = nlohmann::json;
using bhp::http_runtime::HttpMethod;
using bhp::http_runtime::HttpRequest;
using bhp::http_runtime::HttpResponse;

Error error(const char *code, const char *message, bool retryable = false)
{
	return {code, message, {}, retryable};
}

Error invalid_response(const char *where)
{
	return {"MUSIC.RESPONSE_INVALID", "Spotify's answer is outside the documented shape", {{"where", where}}, false};
}

/** A reason code fit to log and show: upper case, digits and underscores, at most 64. */
std::string safe_reason(const Json &body)
{
	if (!body.contains("error") || !body["error"].is_object() || !body["error"].contains("reason") ||
	    !body["error"]["reason"].is_string())
		return {};
	const auto reason = body["error"]["reason"].get<std::string>();
	const bool safe = !reason.empty() && reason.size() <= 64 && std::all_of(reason.begin(), reason.end(), [](unsigned char c) {
		return std::isupper(c) || std::isdigit(c) || c == '_';
	});
	return safe ? reason : std::string();
}

Error status_error(const HttpResponse &response)
{
	const auto status = response.status;
	const Json body = response.content_type.rfind("application/json", 0) == 0
				  ? Json::parse(response.body, nullptr, false)
				  : Json();
	const auto reason = body.is_object() ? safe_reason(body) : std::string();
	Error failure;
	if (status == 403 && reason == "PREMIUM_REQUIRED")
		failure = error("MUSIC.PREMIUM_REQUIRED", "Spotify Premium is required for this");
	else if (status == 404)
		failure = error("MUSIC.OUTPUT_UNAVAILABLE", "Spotify does not see the speaker");
	else if (status == 429)
		failure = error("MUSIC.RATE_LIMITED", "Spotify asked the panel to slow down", true);
	else if (status >= 500)
		failure = error("MUSIC.SPOTIFY_UNAVAILABLE", "Spotify is unavailable", true);
	else
		failure = error("MUSIC.COMMAND_REFUSED", "Spotify refused the command");
	failure.context["http_status"] = std::to_string(status);
	if (!reason.empty())
		failure.context["reason"] = reason;
	return failure;
}

bool read_bool(const Json &object, const char *key, bool &out)
{
	if (!object.contains(key) || !object[key].is_boolean())
		return false;
	out = object[key].get<bool>();
	return true;
}

/** One device object; nullopt when it breaks the documented shape. An absent ID stays empty. */
std::optional<SpotifyDevice> device_from(const Json &value)
{
	SpotifyDevice device;
	if (!value.is_object() || !value.contains("name") || !value["name"].is_string() || !value.contains("type") ||
	    !value["type"].is_string() || !read_bool(value, "is_active", device.is_active) ||
	    !read_bool(value, "is_restricted", device.is_restricted) || !value.contains("id"))
		return std::nullopt;
	if (value["id"].is_string())
		device.id = value["id"].get<std::string>();
	else if (!value["id"].is_null())
		return std::nullopt;
	device.name = value["name"].get<std::string>();
	device.type = value["type"].get<std::string>();
	if (device.name.size() > 256 || device.type.size() > 64)
		return std::nullopt;
	// supports_volume is documented as required; an older answer without it means no control.
	if (value.contains("supports_volume") && !read_bool(value, "supports_volume", device.supports_volume))
		return std::nullopt;
	if (value.contains("volume_percent") && !value["volume_percent"].is_null()) {
		if (!value["volume_percent"].is_number_integer())
			return std::nullopt;
		const auto volume = value["volume_percent"].get<long long>();
		if (volume < 0 || volume > 100)
			return std::nullopt;
		device.volume_percent = static_cast<int>(volume);
	}
	return device;
}
} // namespace

bool valid_device_id(const std::string &device_id)
{
	return !device_id.empty() && device_id.size() <= 128 &&
	       std::all_of(device_id.begin(), device_id.end(),
			   [](unsigned char c) { return std::isalnum(c) || c == '-' || c == '_'; });
}

SpotifyClient::SpotifyClient(SpotifyApiSettings settings, HttpTransport transport, SpotifySession &session)
	: settings_(std::move(settings)), transport_(std::move(transport)), session_(session)
{
}

Result<HttpResponse> SpotifyClient::call(HttpMethod method, const std::string &path, const std::string &json_body)
{
	using Output = Result<HttpResponse>;
	for (int attempt = 0; attempt < 2; ++attempt) {
		auto token = session_.access_token();
		if (!token.has_value())
			return Output::failure(token.error());
		HttpRequest request;
		request.method = method;
		request.url = settings_.api_url + path;
		request.headers = {{"Accept", "application/json"}, {"Authorization", "Bearer " + token.value()}};
		if (!json_body.empty())
			request.headers.emplace_back("Content-Type", "application/json");
		request.body = json_body;
		request.budget = settings_.budget;
		auto response = transport_(request);
		if (!response.has_value())
			return Output::failure({"MUSIC.SPOTIFY_UNREACHABLE", "Spotify did not answer",
						{{"http_code", response.error().code}}, response.error().retryable});
		if (response.value().status != 401)
			return Output::success(response.value());
		// An expired or revoked access token: refresh once, then give up.
		session_.invalidate_access_token();
	}
	return Output::failure(error("MUSIC.SIGN_IN_REQUIRED", "Spotify refused the panel's sign-in"));
}

Result<bool> SpotifyClient::command(HttpMethod method, const std::string &path, const std::string &json_body)
{
	auto response = call(method, path, json_body);
	if (!response.has_value())
		return Result<bool>::failure(response.error());
	const auto status = response.value().status;
	if (status == 200 || status == 202 || status == 204)
		return Result<bool>::success(true);
	return Result<bool>::failure(status_error(response.value()));
}

Result<std::vector<SpotifyDevice>> SpotifyClient::devices()
{
	using Output = Result<std::vector<SpotifyDevice>>;
	auto response = call(HttpMethod::Get, "/me/player/devices", {});
	if (!response.has_value())
		return Output::failure(response.error());
	if (response.value().status != 200)
		return Output::failure(status_error(response.value()));
	const Json body = Json::parse(response.value().body, nullptr, false);
	if (!body.is_object() || !body.contains("devices") || !body["devices"].is_array() || body["devices"].size() > 64)
		return Output::failure(invalid_response("devices"));
	std::vector<SpotifyDevice> devices;
	for (const auto &item : body["devices"]) {
		auto device = device_from(item);
		if (!device)
			return Output::failure(invalid_response("device"));
		if (valid_device_id(device->id))
			devices.push_back(std::move(*device));
	}
	return Output::success(std::move(devices));
}

Result<std::optional<SpotifyPlayback>> SpotifyClient::playback()
{
	using Output = Result<std::optional<SpotifyPlayback>>;
	auto response = call(HttpMethod::Get, "/me/player?additional_types=episode", {});
	if (!response.has_value())
		return Output::failure(response.error());
	if (response.value().status == 204)
		return Output::success(std::nullopt);
	if (response.value().status != 200)
		return Output::failure(status_error(response.value()));
	const Json body = Json::parse(response.value().body, nullptr, false);
	SpotifyPlayback playback;
	if (!body.is_object() || !read_bool(body, "is_playing", playback.is_playing))
		return Output::failure(invalid_response("playback"));
	if (body.contains("device") && !body["device"].is_null()) {
		auto device = device_from(body["device"]);
		if (!device)
			return Output::failure(invalid_response("playback_device"));
		playback.device = std::move(*device);
	}
	playback.item_type = body.value("currently_playing_type", std::string("unknown"));
	if (body.contains("item") && body["item"].is_object()) {
		const Json &item = body["item"];
		if (item.contains("name") && item["name"].is_string())
			playback.title = item["name"].get<std::string>();
		// A track names its artists; an episode names its show.
		if (item.contains("artists") && item["artists"].is_array())
			for (const auto &artist : item["artists"])
				if (artist.is_object() && artist.contains("name") && artist["name"].is_string() &&
				    playback.artists.size() < 8)
					playback.artists.push_back(artist["name"].get<std::string>());
		if (item.contains("show") && item["show"].is_object() && item["show"].contains("name") &&
		    item["show"]["name"].is_string())
			playback.artists.push_back(item["show"]["name"].get<std::string>());
	}
	return Output::success(std::move(playback));
}

Result<bool> SpotifyClient::transfer(const std::string &device_id, bool play)
{
	if (!valid_device_id(device_id))
		return Result<bool>::failure(error("MUSIC.DEVICE_ID_INVALID", "The device ID is not usable"));
	return command(HttpMethod::Put, "/me/player", Json({{"device_ids", {device_id}}, {"play", play}}).dump());
}

Result<bool> SpotifyClient::play(const std::string &device_id)
{
	if (!valid_device_id(device_id))
		return Result<bool>::failure(error("MUSIC.DEVICE_ID_INVALID", "The device ID is not usable"));
	return command(HttpMethod::Put, "/me/player/play?device_id=" + device_id, {});
}

Result<bool> SpotifyClient::pause(const std::string &device_id)
{
	if (!valid_device_id(device_id))
		return Result<bool>::failure(error("MUSIC.DEVICE_ID_INVALID", "The device ID is not usable"));
	return command(HttpMethod::Put, "/me/player/pause?device_id=" + device_id, {});
}

Result<bool> SpotifyClient::next(const std::string &device_id)
{
	if (!valid_device_id(device_id))
		return Result<bool>::failure(error("MUSIC.DEVICE_ID_INVALID", "The device ID is not usable"));
	return command(HttpMethod::Post, "/me/player/next?device_id=" + device_id, {});
}

Result<bool> SpotifyClient::set_volume(const std::string &device_id, int volume_percent)
{
	if (!valid_device_id(device_id))
		return Result<bool>::failure(error("MUSIC.DEVICE_ID_INVALID", "The device ID is not usable"));
	if (volume_percent < 0 || volume_percent > 100)
		return Result<bool>::failure(error("MUSIC.VOLUME_INVALID", "Volume is 0 to 100"));
	return command(HttpMethod::Put,
		       "/me/player/volume?volume_percent=" + std::to_string(volume_percent) + "&device_id=" + device_id, {});
}
} // namespace music_control
