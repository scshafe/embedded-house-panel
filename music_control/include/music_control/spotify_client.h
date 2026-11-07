#pragma once
#include "music_control/spotify_auth.h"
#include <optional>
#include <string>
#include <vector>

/**
 * @canonical spotify_web_api_client -- the panel's Spotify Connect remote (ADR 0013).
 *
 * The Player endpoints only: the device list, the playback state, transfer,
 * play, pause, next and volume. It never plays audio; the speaker streams.
 * Each call takes an access token from the session. A 401 forgets it and
 * retries once; a second 401 is MUSIC.SIGN_IN_REQUIRED. Other answers map to:
 * - MUSIC.PREMIUM_REQUIRED, a 403 with that reason;
 * - MUSIC.COMMAND_REFUSED, any other 403 or 4xx, with the reason in context;
 * - MUSIC.OUTPUT_UNAVAILABLE, a 404 (the device is gone or none is active);
 * - MUSIC.RATE_LIMITED, a 429, retryable;
 * - MUSIC.SPOTIFY_UNAVAILABLE, a 5xx, retryable;
 * - MUSIC.SPOTIFY_UNREACHABLE, no answer, retryable when the transport says so;
 * - MUSIC.RESPONSE_INVALID, a body outside the documented shape.
 */
namespace music_control
{
struct SpotifyApiSettings {
	std::string api_url{"https://api.spotify.com/v1"};
	/** The playback state carries the whole track object, so allow more than the token answers. */
	bhp::http_runtime::TransportBudget budget{2000, 10000, 524288};
};

struct SpotifyDevice {
	std::string id;
	std::string name;
	std::string type;
	bool is_active{false};
	bool is_restricted{false};
	bool supports_volume{false};
	std::optional<int> volume_percent;
};

struct SpotifyPlayback {
	bool is_playing{false};
	/** The device playing, if Spotify names one. */
	std::optional<SpotifyDevice> device;
	/** `track`, `episode`, `ad` or `unknown`. */
	std::string item_type;
	/** The track or episode name; empty for an ad or when no item is reported. */
	std::string title;
	/** A track's artists, or an episode's show. */
	std::vector<std::string> artists;
};

/** Letters, digits, `-` and `_`, 1 to 128: what may go into a `device_id` query. */
bool valid_device_id(const std::string &device_id);

class SpotifyClient final
{
public:
	SpotifyClient(SpotifyApiSettings settings, HttpTransport transport, SpotifySession &session);
	/** `GET /me/player/devices`; devices without an ID are left out, since nothing can target them. */
	Result<std::vector<SpotifyDevice>> devices();
	/** `GET /me/player`; nullopt for 204, when nothing is playing anywhere. */
	Result<std::optional<SpotifyPlayback>> playback();
	/** `PUT /me/player` with `device_ids` and `play`. */
	Result<bool> transfer(const std::string &device_id, bool play);
	/** `PUT /me/player/play?device_id=`: resumes what was playing. */
	Result<bool> play(const std::string &device_id);
	/** `PUT /me/player/pause?device_id=`. */
	Result<bool> pause(const std::string &device_id);
	/** `POST /me/player/next?device_id=`. */
	Result<bool> next(const std::string &device_id);
	/** `PUT /me/player/volume?volume_percent=&device_id=`, 0 to 100. */
	Result<bool> set_volume(const std::string &device_id, int volume_percent);

private:
	Result<bhp::http_runtime::HttpResponse> call(bhp::http_runtime::HttpMethod method, const std::string &path,
						     const std::string &json_body);
	Result<bool> command(bhp::http_runtime::HttpMethod method, const std::string &path, const std::string &json_body);
	SpotifyApiSettings settings_;
	HttpTransport transport_;
	SpotifySession &session_;
};
} // namespace music_control
