// MusicService against the scripted Spotify: the state the UI sees, the commands
// each request sends to the one configured speaker, refusals, and sign-out.
#include "fake_spotify.hpp"
#include "music_control/music_service.h"
#include <unistd.h>
using namespace music_control;
using namespace music_control_test;
namespace wire = music_control::protocol;
namespace
{
const std::string kSpeaker = "5fbb3ba6aa454b5534c4ba43a8c7e8e45a63ad0e";
const std::string kPlayback = "GET /me/player?additional_types=episode";

std::string temporary_directory()
{
	char pattern[] = "/tmp/bhp-music-service-XXXXXX";
	const char *made = ::mkdtemp(pattern);
	if (!made)
		std::exit(2);
	return made;
}

struct Rig {
	FakeSpotify spotify;
	ManualClock clock;
	std::string token_directory{temporary_directory() + "/token"};
	std::unique_ptr<MusicService> service;
	explicit Rig(bool signed_in = true, const std::string &output_name = "Living Room Bose")
	{
		if (signed_in) {
			SpotifyTokenStore store(token_directory);
			(void)store.save(kClientId, "fixture-refresh-1");
			spotify.token(200, fixture("token-refresh-rotated.json"));
		}
		MusicSettings settings;
		settings.auth.client_id = kClientId;
		settings.token_directory = token_directory;
		settings.output_name = output_name;
		MusicPorts ports{std::ref(spotify), clock.function(), [] { return 0.5; }};
		auto opened = MusicService::open(settings, ports);
		if (!opened.has_value())
			std::exit(2);
		service = std::move(opened.value());
	}
	void script_refresh(const std::string &playback = "playback-phone.json")
	{
		spotify.api("GET /me/player/devices", 200, fixture("devices.json"));
		spotify.api(kPlayback, 200, fixture(playback));
	}
	const wire::Refused *refused(const std::vector<wire::MusicEvent> &events)
	{
		return events.empty() ? nullptr : std::get_if<wire::Refused>(&events.front());
	}
};

void signed_out()
{
	Rig rig(false);
	auto state = rig.service->state();
	expect(state.sign_in == "signed_out" && state.output.state == "unknown" && state.output.name == "Living Room Bose" &&
		       !state.playback,
	       "without a token the panel shows signed out");
	expect(rig.service->tick() == std::chrono::seconds(60) && rig.spotify.requests.empty(),
	       "signed out, nothing is sent");
	auto events = rig.service->handle(wire::SelectOutputRequest{});
	const auto *refused = rig.refused(events);
	expect(refused && refused->request == "select_output" && refused->error_code == "MUSIC.SIGN_IN_REQUIRED" &&
		       events.size() == 2,
	       "a request while signed out is refused, then the state follows");
}

void flow()
{
	Rig rig;
	const auto first = rig.service->state();
	expect(first.sign_in == "signed_in" && first.output.state == "unknown", "signed in, not asked yet");
	rig.script_refresh();
	const auto wait = rig.service->tick();
	auto state = rig.service->state();
	expect(state.output.state == "available" && state.output.volume_percent == 35u && state.output.supports_volume &&
		       !state.playback && state.sequence > first.sequence,
	       "the speaker is listed while the phone plays");
	expect(wait == std::chrono::seconds(15), "the next refresh is in 15 s");
	expect(rig.service->state().sequence == state.sequence, "the sequence rises only on change");
	const auto requests = rig.spotify.requests.size();
	rig.clock.now += std::chrono::seconds(5);
	(void)rig.service->tick();
	expect(rig.spotify.requests.size() == requests, "nothing is sent before the refresh is due");

	rig.spotify.api("PUT /me/player", 204);
	auto selected = rig.service->handle(wire::SelectOutputRequest{});
	expect(selected.size() == 1 && std::get<wire::State>(selected.front()).output.state == "active" &&
		       std::get<wire::State>(selected.front()).playback && std::get<wire::State>(selected.front()).playback->is_playing,
	       "select moves playback to the speaker and shows it at once");
	expect(rig.spotify.api_calls().back() == "PUT /me/player", "select transfers playback");
	rig.clock.now += std::chrono::milliseconds(1500);
	rig.script_refresh("playback-speaker.json");
	(void)rig.service->tick();
	state = rig.service->state();
	expect(state.output.state == "active" && state.playback && state.playback->title == "Slow Sunrise — Live" &&
		       state.playback->artists == "The Fixture Band, Café Ensemble",
	       "after the settle delay the track is read back");

	rig.spotify.api("PUT /me/player/pause?device_id=" + kSpeaker, 204);
	auto paused = rig.service->handle(wire::PauseRequest{});
	expect(std::get<wire::State>(paused.front()).playback && !std::get<wire::State>(paused.front()).playback->is_playing,
	       "pause");
	rig.spotify.api("PUT /me/player/play?device_id=" + kSpeaker, 204);
	(void)rig.service->handle(wire::PlayRequest{});
	rig.spotify.api("POST /me/player/next?device_id=" + kSpeaker, 204);
	(void)rig.service->handle(wire::NextRequest{});
	rig.spotify.api("PUT /me/player/volume?volume_percent=50&device_id=" + kSpeaker, 204);
	auto louder = rig.service->handle(wire::VolumeRequest{50});
	expect(std::get<wire::State>(louder.front()).output.volume_percent == 50u, "volume");
	const auto calls = rig.spotify.api_calls();
	expect(std::vector<std::string>(calls.end() - 4, calls.end()) ==
		       std::vector<std::string>{"PUT /me/player/pause?device_id=" + kSpeaker,
						"PUT /me/player/play?device_id=" + kSpeaker,
						"POST /me/player/next?device_id=" + kSpeaker,
						"PUT /me/player/volume?volume_percent=50&device_id=" + kSpeaker},
	       "pause, play, next and volume reach the configured speaker only");

	// What the UI receives never names a device ID or a token.
	auto frame = wire::encode(wire::MusicEvent(rig.service->state()));
	expect(frame.has_value() && frame.value().find(kSpeaker) == std::string::npos &&
		       frame.value().find("fixture-") == std::string::npos,
	       "the state frame carries no device ID or token");

	rig.spotify.api("PUT /me/player/play?device_id=" + kSpeaker, 404, fixture("error-404-no-device.json"));
	auto gone = rig.service->handle(wire::PlayRequest{});
	expect(rig.refused(gone) && rig.refused(gone)->error_code == "MUSIC.OUTPUT_UNAVAILABLE" &&
		       std::get<wire::State>(gone.back()).output.state == "missing",
	       "a 404 forgets the speaker until the next device list");
}

void refusals()
{
	Rig missing(true, "Bedroom Bose");
	missing.script_refresh();
	(void)missing.service->tick();
	expect(missing.service->state().output.state == "missing", "a speaker not in the list is missing");
	missing.script_refresh();
	auto events = missing.service->handle(wire::PlayRequest{});
	expect(missing.refused(events) && missing.refused(events)->error_code == "MUSIC.OUTPUT_UNAVAILABLE",
	       "play with the speaker missing is refused");

	Rig phone(true, "Kitchen phone");
	phone.script_refresh();
	(void)phone.service->tick();
	auto volume = phone.service->handle(wire::VolumeRequest{20});
	expect(phone.refused(volume) && phone.refused(volume)->error_code == "MUSIC.VOLUME_UNSUPPORTED",
	       "volume on a device without volume control is refused");

	Rig restricted(true, "Restricted Bose");
	restricted.spotify.api("GET /me/player/devices", 200,
			       R"({"devices":[{"id":"abc123","is_active":false,"is_private_session":false,"is_restricted":true,"name":"Restricted Bose","type":"Speaker","volume_percent":10,"supports_volume":true}]})");
	restricted.spotify.api(kPlayback, 204);
	(void)restricted.service->tick();
	expect(restricted.service->state().output.state == "restricted", "a restricted speaker is shown as restricted");
	auto play = restricted.service->handle(wire::PlayRequest{});
	expect(restricted.refused(play) && restricted.refused(play)->error_code == "MUSIC.OUTPUT_RESTRICTED",
	       "commands to a restricted speaker are refused locally");
}

void failures_and_sign_out()
{
	Rig rig;
	rig.spotify.api("GET /me/player/devices", 503, "");
	const auto wait = rig.service->tick();
	expect(rig.service->state().error == "MUSIC.SPOTIFY_UNAVAILABLE", "a failed refresh shows its code");
	expect(wait >= std::chrono::seconds(4) && wait <= std::chrono::seconds(5), "a retryable failure backs off");
	rig.clock.now += wait;
	rig.script_refresh();
	(void)rig.service->tick();
	expect(rig.service->state().error.empty() && rig.service->state().output.state == "available",
	       "the next refresh clears the error");

	// Spotify revokes the token: the next refresh signs the panel out and clears the file.
	rig.clock.now += std::chrono::hours(2);
	rig.spotify.token(400, fixture("token-invalid-grant.json"));
	(void)rig.service->tick();
	auto state = rig.service->state();
	expect(state.sign_in == "signed_out" && state.error == "MUSIC.SIGN_IN_REQUIRED" && state.output.state == "unknown",
	       "invalid_grant shows sign in again");
	// The service's store holds the directory's lock, so read the file itself.
	std::ifstream saved(rig.token_directory + "/spotify-token.json");
	const std::string record{std::istreambuf_iterator<char>(saved), std::istreambuf_iterator<char>()};
	expect(record.find("\"signed_out\":true") != std::string::npos && record.find("fixture-refresh") == std::string::npos,
	       "the token file holds a signed-out record");
}

void text()
{
	expect(bounded_text("plain", 16) == "plain", "short text is kept");
	expect(bounded_text("a\tb\nc", 16) == "a b c", "control characters become spaces");
	const auto cut = bounded_text("Café Café Café", 8);
	expect(cut == "Caf\xE2\x80\xA6" || cut == "Café\xE2\x80\xA6", "a cut never splits a character");
	expect(cut.size() <= 8, "a cut fits the bound");
}
} // namespace

int main()
{
	signed_out();
	flow();
	refusals();
	failures_and_sign_out();
	text();
	if (failures == 0)
		std::cout << "music_control_music_service_test passed\n";
	return failures == 0 ? 0 : 1;
}
