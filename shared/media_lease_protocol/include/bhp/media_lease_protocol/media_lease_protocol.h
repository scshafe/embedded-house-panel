#pragma once
#include "bhp/media_window_protocol/media_window_protocol.h"
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

/**
 * @canonical media_lease_protocol -- the UI-to-broker lease exchange (ADR 0005,
 * voice-take capture plan Steps 5 and 6).
 *
 * Same framing as the media window protocol: one compact JSON object per line,
 * at most 4096 bytes, closed key sets, `version: 1`. The UI sends `arm`,
 * `start` and `stop`; the broker answers `armed` or `refused`, then `started`,
 * `level` about every 100 ms while recording, and exactly one `ended` per
 * started lease. `discard` deletes a committed take (`discarded` or `refused`,
 * whose request ID is then the take ID). `hand_off` gives a kept take to the
 * voice-lab uploader (`handed_off` or `refused` the same way; recorder wire
 * plan Decision 3). The UI never sends audio, a provenance claim or a digest: it
 * names a touch observation, and the broker claims it from the supervisor.
 */
namespace bhp::media_lease_protocol
{
using media_window_protocol::Done;
using media_window_protocol::Error;
using media_window_protocol::PresentationBinding;
using media_window_protocol::Result;

inline constexpr std::uint32_t kProtocolVersion = 1;
inline constexpr std::size_t kMaximumChannels = 8;

/** Purposes the wire can name. Only voice_take is served by the real broker. */
enum class Purpose { VoiceTake, CommandRecognition, WishRecording };
std::string_view to_string(Purpose purpose);

struct ArmRequest {
	std::string request_id;
	Purpose purpose{Purpose::VoiceTake};
	std::string touch_observation_id;
	PresentationBinding presentation_binding;
	/** What the take is for, opaque to the broker (a prompt reference): 1..128 printable ASCII. */
	std::string operation_ref;
};
struct StartRequest {
	std::string lease_id;
};
struct StopRequest {
	std::string lease_id;
};
/** Deletes a committed take the reviewer rejected; only while no lease is active. */
struct DiscardRequest {
	std::string take_id;
};
/** Moves a committed take the owner kept into the uploader's hand-off directory; only while no lease is active. */
struct HandOffRequest {
	std::string take_id;
};
using UiRequest = std::variant<ArmRequest, StartRequest, StopRequest, DiscardRequest, HandOffRequest>;

struct Armed {
	std::string request_id;
	std::string lease_id;
};
struct Refused {
	std::string request_id;
	std::string error_code;
};
struct Started {
	std::string lease_id;
};
/** Peak per channel over the last interval, in tenths of a dBFS (-1200 is silence). */
struct Level {
	std::string lease_id;
	std::uint64_t elapsed_milliseconds{0};
	std::vector<std::int32_t> peak_decibels_tenths;
};
enum class Outcome { Committed, Failed };
/** Committed: take_id, stop_reason and frames are set. Failed: error_code is set. */
struct Ended {
	std::string lease_id;
	Outcome outcome{Outcome::Failed};
	std::string take_id;
	std::string stop_reason;
	std::uint64_t frames{0};
	std::string error_code;
};
struct Discarded {
	std::string take_id;
};
struct HandedOff {
	std::string take_id;
};
using BrokerEvent = std::variant<Armed, Refused, Started, Level, Ended, Discarded, HandedOff>;

/**
 * @canonical media_lease_client -- the UI's side of one broker connection.
 *
 * Connects to the broker's UI socket and checks that the peer is the exact
 * broker UID. It sends requests and returns broker events in order; it holds
 * no audio and no claim. A malformed event or a lost broker closes it, and
 * the caller treats any lease it held as ended.
 */
class MediaLeaseClient final
{
public:
	static Result<std::unique_ptr<MediaLeaseClient>> connect(const std::string &path, std::uint32_t broker_uid);
	/** Test seam: an already-connected stream socket; the peer UID is still checked. */
	static Result<std::unique_ptr<MediaLeaseClient>> for_connected_peer(int descriptor, std::uint32_t broker_uid);
	Result<Done> send(const UiRequest &request);
	/** Events received so far (at most 16 frames per call). */
	Result<std::vector<BrokerEvent>> poll();
	bool connected() const { return channel_ && channel_->open(); }
	void close() noexcept;

private:
	explicit MediaLeaseClient(int descriptor)
		: channel_(std::make_unique<media_window_protocol::FrameChannel>(descriptor))
	{
	}
	std::unique_ptr<media_window_protocol::FrameChannel> channel_;
};

Result<std::string> encode(const UiRequest &request);
Result<std::string> encode(const BrokerEvent &event);
Result<UiRequest> decode_ui_request(std::string_view frame);
Result<BrokerEvent> decode_broker_event(std::string_view frame);
} // namespace bhp::media_lease_protocol
