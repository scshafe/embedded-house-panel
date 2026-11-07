// The Web API client against the scripted Spotify: what it sends for each player
// command, how it reads the fixtures, and how each failure maps to a MUSIC code.
#include "fake_spotify.hpp"
#include "music_control/spotify_client.h"
#include <nlohmann/json.hpp>
#include <unistd.h>
using namespace music_control;
using namespace music_control_test;
namespace
{
const std::string kSpeaker = "5fbb3ba6aa454b5534c4ba43a8c7e8e45a63ad0e";

std::string temporary_directory()
{
	char pattern[] = "/tmp/bhp-music-client-XXXXXX";
	const char *made = ::mkdtemp(pattern);
	if (!made)
		std::exit(2);
	return made;
}

struct Rig {
	FakeSpotify spotify;
	ManualClock clock;
	SpotifyTokenStore store{temporary_directory() + "/token"};
	SpotifySession session;
	SpotifyClient client;
	Rig()
		: session(
			  [] {
				  SpotifyAuthSettings value;
				  value.client_id = kClientId;
				  return value;
			  }(),
			  std::ref(spotify), store, clock.function()),
		  client({}, std::ref(spotify), session)
	{
		(void)store.save(kClientId, "fixture-refresh-1");
		(void)session.restore();
		spotify.token(200, fixture("token-refresh-rotated.json"));
	}
};

void reads()
{
	Rig rig;
	rig.spotify.api("GET /me/player/devices", 200, fixture("devices.json"));
	auto devices = rig.client.devices();
	expect(devices.has_value() && devices.value().size() == 2, "devices without an ID are left out");
	if (devices.has_value() && devices.value().size() == 2) {
		const auto &speaker = devices.value()[0];
		expect(speaker.id == kSpeaker && speaker.name == "Living Room Bose" && speaker.type == "Speaker" &&
			       !speaker.is_active && !speaker.is_restricted && speaker.supports_volume &&
			       speaker.volume_percent == 35,
		       "the speaker's device record");
		expect(devices.value()[1].is_active && !devices.value()[1].supports_volume, "the phone's device record");
	}
	const auto &request = rig.spotify.requests.back();
	bool bearer = false;
	for (const auto &header : request.headers)
		bearer = bearer || (header.first == "Authorization" && header.second == "Bearer fixture-access-2");
	expect(bearer && request.method == HttpMethod::Get && request.body.empty(), "a GET with the access token");

	rig.spotify.api("GET /me/player?additional_types=episode", 200, fixture("playback-speaker.json"));
	auto playback = rig.client.playback();
	expect(playback.has_value() && playback.value().has_value(), "playback read");
	if (playback.has_value() && playback.value()) {
		const auto &state = *playback.value();
		expect(state.is_playing && state.item_type == "track" && state.title == "Slow Sunrise — Live" &&
			       state.artists == std::vector<std::string>{"The Fixture Band", "Café Ensemble"} &&
			       state.device && state.device->id == kSpeaker,
		       "the track, its artists and the device");
	}
	rig.spotify.api("GET /me/player?additional_types=episode", 204);
	auto idle = rig.client.playback();
	expect(idle.has_value() && !idle.value().has_value(), "204 means nothing is playing");
	rig.spotify.api("GET /me/player/devices", 200, R"({"devices":[{"id":"x","name":7}]})");
	auto broken = rig.client.devices();
	expect(!broken.has_value() && broken.error().code == "MUSIC.RESPONSE_INVALID", "a malformed device list is refused");
	rig.spotify.api("GET /me/player/devices", 200, "not json");
	expect(rig.client.devices().error().code == "MUSIC.RESPONSE_INVALID", "a body that is not JSON is refused");
}

void commands()
{
	Rig rig;
	rig.spotify.api("PUT /me/player", 204);
	expect(rig.client.transfer(kSpeaker, true).has_value(), "transfer");
	const auto body = nlohmann::json::parse(rig.spotify.requests.back().body);
	expect(body == nlohmann::json({{"device_ids", {kSpeaker}}, {"play", true}}), "transfer names the device and plays");
	rig.spotify.api("PUT /me/player/play?device_id=" + kSpeaker, 204);
	rig.spotify.api("PUT /me/player/pause?device_id=" + kSpeaker, 204);
	rig.spotify.api("POST /me/player/next?device_id=" + kSpeaker, 204);
	rig.spotify.api("PUT /me/player/volume?volume_percent=40&device_id=" + kSpeaker, 204);
	expect(rig.client.play(kSpeaker).has_value(), "play");
	expect(rig.client.pause(kSpeaker).has_value(), "pause");
	expect(rig.client.next(kSpeaker).has_value(), "next");
	expect(rig.client.set_volume(kSpeaker, 40).has_value(), "volume");
	const auto calls = rig.spotify.api_calls();
	expect(calls == std::vector<std::string>{"PUT /me/player", "PUT /me/player/play?device_id=" + kSpeaker,
						 "PUT /me/player/pause?device_id=" + kSpeaker,
						 "POST /me/player/next?device_id=" + kSpeaker,
						 "PUT /me/player/volume?volume_percent=40&device_id=" + kSpeaker},
	       "each command's method and path");
	const auto before = rig.spotify.requests.size();
	expect(rig.client.play("../me").error().code == "MUSIC.DEVICE_ID_INVALID", "an unsafe device ID is refused");
	expect(rig.client.set_volume(kSpeaker, 101).error().code == "MUSIC.VOLUME_INVALID", "volume above 100 is refused");
	expect(rig.spotify.requests.size() == before, "refused locally, nothing is sent");
}

void failures_map()
{
	Rig rig;
	const auto play = "PUT /me/player/play?device_id=" + kSpeaker;
	rig.spotify.api(play, 403, fixture("error-403-premium.json"));
	expect(rig.client.play(kSpeaker).error().code == "MUSIC.PREMIUM_REQUIRED", "403 PREMIUM_REQUIRED");
	rig.spotify.api(play, 404, fixture("error-404-no-device.json"));
	auto missing = rig.client.play(kSpeaker);
	expect(missing.error().code == "MUSIC.OUTPUT_UNAVAILABLE" && missing.error().context.at("reason") == "NO_ACTIVE_DEVICE",
	       "404 is the speaker unavailable, with the reason");
	rig.spotify.api(play, 429, fixture("error-429.json"));
	auto limited = rig.client.play(kSpeaker);
	expect(limited.error().code == "MUSIC.RATE_LIMITED" && limited.error().retryable, "429 is retryable");
	rig.spotify.api(play, 503, "");
	auto down = rig.client.play(kSpeaker);
	expect(down.error().code == "MUSIC.SPOTIFY_UNAVAILABLE" && down.error().retryable, "503 is retryable");
	rig.spotify.api(play, 400, R"({"error":{"status":400,"message":"bad","reason":"<script>"}})");
	auto refused = rig.client.play(kSpeaker);
	expect(refused.error().code == "MUSIC.COMMAND_REFUSED" && refused.error().context.count("reason") == 0,
	       "an unsafe reason is dropped");

	// A 401 refreshes once and retries; a second 401 means signing in again.
	rig.spotify.api(play, 401, fixture("error-401.json"));
	rig.spotify.api(play, 204);
	rig.spotify.token(200, fixture("token-refresh-kept.json"));
	expect(rig.client.play(kSpeaker).has_value(), "a 401 refreshes and retries once");
	rig.spotify.api(play, 401, fixture("error-401.json"));
	rig.spotify.api(play, 401, fixture("error-401.json"));
	rig.spotify.token(200, fixture("token-refresh-kept.json"));
	expect(rig.client.play(kSpeaker).error().code == "MUSIC.SIGN_IN_REQUIRED", "two 401s mean signing in again");
	rig.spotify.unreachable = true;
	auto offline = rig.client.devices();
	expect(offline.error().code == "MUSIC.SPOTIFY_UNREACHABLE" && offline.error().retryable, "no answer");
}
} // namespace

int main()
{
	reads();
	commands();
	failures_map();
	if (failures == 0)
		std::cout << "music_control_spotify_client_test passed\n";
	return failures == 0 ? 0 : 1;
}
