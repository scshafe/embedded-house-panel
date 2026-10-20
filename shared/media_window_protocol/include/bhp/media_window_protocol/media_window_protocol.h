#pragma once
#include "bhp/touch_observation_protocol/types.h"
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

/**
 * @canonical media_window_protocol -- the supervisor-to-broker lifecycle window
 * and touch claim exchange (ADR 0005, voice-take capture plan Step 4).
 *
 * One newline-terminated JSON object per frame, at most 4096 bytes, closed key
 * sets, no duplicate keys. The supervisor sends `window` and `claim_result`;
 * the broker sends only `claim`. Every supervisor frame carries the
 * supervisor session ID and a sequence that rises by exactly one, so the
 * broker treats a new session, a gap or a replay as a closed window.
 */
namespace bhp::media_window_protocol
{
inline constexpr std::uint32_t kProtocolVersion = 1;
inline constexpr std::size_t kMaximumFrameBytes = 4096;

struct Error {
	std::string code;
	std::string message;
	std::map<std::string, std::string> context;
};

template <typename Value> class Result final
{
public:
	static Result success(Value value) { return Result(std::move(value)); }
	static Result failure(Error error) { return Result(std::move(error)); }
	bool has_value() const { return std::holds_alternative<Value>(storage_); }
	explicit operator bool() const { return has_value(); }
	const Value &value() const { return std::get<Value>(storage_); }
	Value &value() { return std::get<Value>(storage_); }
	const Error &error() const { return std::get<Error>(storage_); }

private:
	explicit Result(Value value) : storage_(std::move(value)) {}
	explicit Result(Error error) : storage_(std::move(error)) {}
	std::variant<Value, Error> storage_;
};

struct Done {
};

/** Why the window is in its state. Only `day_present` opens it. */
enum class WindowReason { DayPresent, Absence, Night, SuspendPreparation, SupervisorStopping };
std::string_view to_string(WindowReason reason);

struct WindowMessage {
	std::string supervisor_session_id;
	std::uint64_t sequence{0};
	bool open{false};
	WindowReason reason{WindowReason::Absence};
};

using PresentationBinding = touch_observation_protocol::LifecyclePresentationBinding;

/** The broker's request to consume one touch observation for one operation. */
struct ClaimRequest {
	std::string request_id;
	std::string touch_observation_id;
	std::string canonical_operation_digest;
	PresentationBinding presentation_binding;
};

/** What the supervisor attests about a consumed observation. */
struct ClaimEnvelope {
	std::string touch_observation_id;
	std::uint64_t observation_sequence{0};
	std::string canonical_operation_digest;
	PresentationBinding presentation_binding;
	std::uint64_t contact_duration_milliseconds{0};
};

/** Exactly one of `envelope` or `error_code` is set. */
struct ClaimResult {
	std::string supervisor_session_id;
	std::uint64_t sequence{0};
	std::string request_id;
	std::optional<ClaimEnvelope> envelope;
	std::string error_code;
};

using SupervisorMessage = std::variant<WindowMessage, ClaimResult>;

Result<std::string> encode(const WindowMessage &message);
Result<std::string> encode(const ClaimResult &message);
Result<std::string> encode(const ClaimRequest &message);
/** One frame including its newline. MEDIA_WINDOW.WIRE_INVALID on anything else. */
Result<SupervisorMessage> decode_supervisor_message(std::string_view frame);
Result<ClaimRequest> decode_claim_request(std::string_view frame);

/**
 * A nonblocking stream socket carrying whole frames. Reads are bounded to one
 * frame; an over-long line, a closed peer or a short write ends the channel.
 */
class FrameChannel final
{
public:
	/**
	 * Takes ownership of a connected, nonblocking descriptor. `maximum_frame_bytes`
	 * bounds one frame; the media protocols keep 4096, the voice-lab uploader's
	 * protocol uses more (recorder wire plan Decision 4).
	 */
	explicit FrameChannel(int descriptor, std::size_t maximum_frame_bytes = kMaximumFrameBytes)
		: descriptor_(descriptor), maximum_frame_bytes_(maximum_frame_bytes)
	{
	}
	~FrameChannel();
	FrameChannel(const FrameChannel &) = delete;
	FrameChannel &operator=(const FrameChannel &) = delete;
	/** The next complete frame, an empty optional if none yet, or an error (channel closed). */
	Result<std::optional<std::string>> receive();
	/** One whole-frame send; anything less closes the channel. */
	Result<Done> send(const std::string &frame);
	bool open() const { return descriptor_ >= 0; }
	int descriptor() const { return descriptor_; }
	void close() noexcept;

private:
	int descriptor_;
	std::size_t maximum_frame_bytes_;
	std::string buffered_;
};

/**
 * @canonical media_window_client -- the broker's view of the supervisor window.
 *
 * Connects only to a root-owned socket under root-owned, non-writable parents
 * whose peer is the expected supervisor UID. The window is open only while the
 * last frame said so and every frame so far came from one supervisor session
 * with a sequence one higher than the one before. A new session, a gap, a
 * replay, a malformed frame or a lost connection closes the window for good:
 * the broker reconnects and waits for a fresh `open`.
 */
class MediaWindowClient final
{
public:
	static Result<std::unique_ptr<MediaWindowClient>> connect(const std::string &path, std::uint32_t supervisor_uid);
	/** Test seam: an already-connected stream socket; the peer UID is still checked. */
	static Result<std::unique_ptr<MediaWindowClient>> for_connected_peer(int descriptor, std::uint32_t supervisor_uid);

	struct Update {
		/** The window changed (opened or closed) during this poll. */
		bool window_changed{false};
		std::vector<ClaimResult> claim_results;
	};
	/** Reads at most four frames. A failure closes the window and the channel. */
	Result<Update> poll();
	Result<Done> claim(const ClaimRequest &request);
	bool window_open() const { return open_ && channel_ && channel_->open(); }
	bool connected() const { return channel_ && channel_->open(); }
	WindowReason last_reason() const { return reason_; }
	void close() noexcept;

private:
	explicit MediaWindowClient(int descriptor) : channel_(std::make_unique<FrameChannel>(descriptor)) {}
	std::unique_ptr<FrameChannel> channel_;
	std::string session_;
	std::uint64_t sequence_{0};
	bool open_{false};
	WindowReason reason_{WindowReason::Absence};
};

/** The peer's kernel-reported UID, or empty when SO_PEERCRED fails. */
std::optional<std::uint32_t> peer_uid(int descriptor);
} // namespace bhp::media_window_protocol
