#pragma once
#include "bhp/durable_store/durable_store.h"
#include "music_control/music_protocol.h"
#include "music_control/spotify_client.h"
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

/**
 * @canonical music_control_service -- what `bhp-music-control` does (ADR 0013).
 *
 * The one holder of the Spotify refresh token. It answers the UI's requests
 * (music-control protocol) for the one configured speaker, found by its exact
 * Spotify device name, and in `tick()` re-reads the device list and the
 * playback state. It sends commands only; no audio passes through it, and it
 * opens no audio device. Nothing here logs a token or a device ID.
 */
namespace music_control
{
struct MusicSettings {
	SpotifyAuthSettings auth;
	SpotifyApiSettings api;
	/** Owner-only: `spotify-token.json` (ADR 0013). */
	std::string token_directory;
	/** The speaker's Spotify device name, exactly as the Spotify app shows it. */
	std::string output_name;
	/** How often the device list and playback state are re-read while signed in. */
	std::chrono::seconds refresh_interval{15};
	/** After a command, when to re-read: Spotify does not order player commands. */
	std::chrono::milliseconds settle{1500};
	bhp::durable_store::BackoffPolicy backoff{std::chrono::seconds(5), std::chrono::minutes(5), 0.2};
};

struct MusicPorts {
	HttpTransport transport;
	std::function<std::chrono::steady_clock::time_point()> steady_now;
	std::function<double()> random01;
};

/** Printable text of at most `maximum` bytes, cut on a UTF-8 boundary, with an ellipsis when cut. */
std::string bounded_text(const std::string &text, std::size_t maximum);

class MusicService final
{
public:
	/** MUSIC.SETTINGS_INVALID for a bad client ID or speaker name; a bad token file only signs out. */
	static Result<std::unique_ptr<MusicService>> open(MusicSettings settings, MusicPorts ports);

	/** Answers one UI request with the events to send back, in order. */
	std::vector<protocol::MusicEvent> handle(const protocol::UiRequest &request);
	/** One round of background work; how long until it wants another. */
	std::chrono::milliseconds tick();
	/** The state the UI shows; its sequence rises whenever any of it changed. */
	protocol::State state();

private:
	MusicService(MusicSettings settings, MusicPorts ports, bhp::durable_store::Backoff backoff);
	/** Re-reads the device list and the playback state. */
	Result<bool> refresh();
	Result<bool> command(const protocol::UiRequest &request, const SpotifyDevice &device);

	MusicSettings settings_;
	MusicPorts ports_;
	SpotifyTokenStore store_;
	SpotifySession session_;
	SpotifyClient client_;
	bhp::durable_store::Backoff backoff_;
	/** The configured speaker's last device record, while Spotify lists it. */
	std::optional<SpotifyDevice> output_;
	std::optional<SpotifyPlayback> playback_;
	bool asked_{false};
	std::string error_;
	std::chrono::steady_clock::time_point due_{};
	protocol::State last_;
};
} // namespace music_control
