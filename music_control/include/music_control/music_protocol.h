#pragma once
#include "music_control/result.h"
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

/**
 * @canonical music_control_protocol -- the UI-to-music-control exchange (ADR 0013).
 *
 * ADR 0005's framing: one compact JSON object per line with `version: 1` and
 * closed key sets, over a Unix socket whose peer is checked by exact UID both
 * ways, frames up to 16 KiB.
 *
 * The UI sends what someone at the panel does: `status`, `select_output` (play
 * on the configured speaker), `play`, `pause`, `next` and `volume`. The service
 * answers with `state` whenever its state changes, and with `refused` for a
 * request it cannot do. Neither direction names a device ID or carries a
 * token: the UI can only ever reach the one configured speaker.
 */
namespace music_control::protocol
{
inline constexpr std::uint32_t kProtocolVersion = 1;
inline constexpr std::size_t kMaximumFrameBytes = 16 * 1024;
/** Bytes of a title or of the joined artists, after the service shortens them. */
inline constexpr std::size_t kMaximumTextBytes = 512;

struct StatusRequest {
};
/** Moves Spotify playback to the configured speaker and starts it. */
struct SelectOutputRequest {
};
struct PlayRequest {
};
struct PauseRequest {
};
struct NextRequest {
};
struct VolumeRequest {
	/** 0 to 100. */
	std::uint32_t percent{0};
};
using UiRequest =
	std::variant<StatusRequest, SelectOutputRequest, PlayRequest, PauseRequest, NextRequest, VolumeRequest>;

/** The configured speaker as Spotify reports it. */
struct Output {
	/**
	 * `unknown` (not asked yet, or signed out), `missing` (not in the device
	 * list: asleep, off or another network), `restricted` (Spotify accepts no
	 * commands for it), `available` or `active` (the device playing).
	 */
	std::string state{"unknown"};
	/** The configured name; always present. */
	std::string name;
	bool supports_volume{false};
	std::optional<std::uint32_t> volume_percent;
};

/** What plays on the configured speaker; absent when it is not the active device. */
struct Playback {
	bool is_playing{false};
	std::string title;
	/** The artists joined with ", ", or an episode's show. */
	std::string artists;
};

struct State {
	std::uint64_t sequence{0};
	/** `signed_in` or `signed_out` (sign in again on the PC). */
	std::string sign_in{"signed_out"};
	Output output;
	std::optional<Playback> playback;
	/** The last failure to show (a MUSIC.* code), or empty. */
	std::string error;
};

struct Refused {
	/** `select_output`, `play`, `pause`, `next` or `volume`. */
	std::string request;
	std::string error_code;
};
using MusicEvent = std::variant<State, Refused>;

/** The wire name of a request, as `Refused::request` uses it. */
std::string request_name(const UiRequest &request);
Result<std::string> encode(const UiRequest &request);
Result<std::string> encode(const MusicEvent &event);
/** MUSIC.PROTOCOL_INVALID for anything but one whole, valid frame. */
Result<UiRequest> decode_ui_request(std::string_view frame);
Result<MusicEvent> decode_music_event(std::string_view frame);

/**
 * @canonical music_control_client -- the UI's side of one music-control connection.
 *
 * Connects to the service's socket and checks that the peer is the exact
 * service UID. It sends requests and returns events in order; a malformed
 * event or a lost service closes it (MUSIC.CONTROL_UNAVAILABLE).
 */
class MusicClient final
{
public:
	static Result<std::unique_ptr<MusicClient>> connect(const std::string &path, std::uint32_t service_uid);
	/** Test seam: an already-connected stream socket; the peer UID is still checked. */
	static Result<std::unique_ptr<MusicClient>> for_connected_peer(int descriptor, std::uint32_t service_uid);
	~MusicClient();
	MusicClient(const MusicClient &) = delete;
	MusicClient &operator=(const MusicClient &) = delete;
	Result<bool> send(const UiRequest &request);
	Result<std::vector<MusicEvent>> poll();
	bool connected() const;

private:
	struct Channel;
	explicit MusicClient(int descriptor);
	std::unique_ptr<Channel> channel_;
};
} // namespace music_control::protocol
