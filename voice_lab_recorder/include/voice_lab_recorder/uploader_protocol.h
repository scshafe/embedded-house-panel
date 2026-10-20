#pragma once
#include "voice_lab_recorder/result.h"
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

/**
 * @canonical voice_lab_uploader_protocol -- the UI-to-uploader exchange (recorder wire plan Decision 4).
 *
 * ADR 0005's framing: one compact JSON object per line with `version: 1` and
 * closed key sets, over a Unix socket whose peer is checked by exact UID both
 * ways. Frames reach `kMaximumFrameBytes` (64 KiB), since the catalog's prompts
 * do not fit 4,096 bytes.
 *
 * The UI sends what the owner does: `status`, `sign_in`, `approve` (naming the
 * digest the owner reviewed) and `keep` (a take the broker handed off, for a
 * prompt). The uploader answers with `state` whenever its state changes, and
 * with `refused` for a request it cannot do. The UI never holds a token and
 * never sees a refresh token or an access token: `state` carries only what
 * the owner must see (the verification URI and user code, the approval to
 * review) and what the recorder shows (the prompts and the outbox).
 */
namespace voice_lab_recorder::uploader_protocol
{
inline constexpr std::uint32_t kProtocolVersion = 1;
inline constexpr std::size_t kMaximumFrameBytes = 64 * 1024;
inline constexpr std::size_t kMaximumPrompts = 200;

struct StatusRequest {
};
struct SignInRequest {
};
/** Confirms the approval preview the owner reviewed: its exact digest. */
struct ApproveRequest {
	std::string approval_sha256;
};
/** A take the broker handed off, kept for a prompt of the current catalog version. */
struct KeepRequest {
	std::string take_id;
	std::string prompt_id;
	/** RFC 3339 UTC: when the prompt was put on the screen. */
	std::string requested_at;
};
using UiRequest = std::variant<StatusRequest, SignInRequest, ApproveRequest, KeepRequest>;

/** `signed_in`, `signed_out`, `pending` (a code is on the screen) or `expired`. */
struct SignIn {
	std::string state{"signed_out"};
	std::string verification_uri;
	std::string user_code;
	std::uint64_t expires_in_seconds{0};
};

/**
 * `in_force`; `preview` (a record to review and confirm, with its digest);
 * `unavailable` (the PC's node cannot be resolved, so nothing can be approved).
 */
struct Approval {
	std::string state{"unavailable"};
	std::string endpoint;
	std::string node_stable_id;
	std::string expires_at;
	std::string approval_sha256;
};

/** One prompt of the current catalog version with its status in the panel's view. */
struct Prompt {
	std::string prompt_id;
	std::string text;
	std::string category;
	/** `missing`, `draft`, `kept` or `needs-attention`. */
	std::string status;
};

struct State {
	std::uint64_t sequence{0};
	SignIn sign_in;
	Approval approval;
	std::uint64_t queued{0};
	std::uint64_t kept{0};
	std::uint64_t refused{0};
	/** Why the outbox waits (a VOICE_LAB.* code), or empty. */
	std::string blocked;
	std::optional<std::uint64_t> catalog_version;
	std::vector<Prompt> prompts;
	/** The last voice-lab failure the owner should see (a VOICE_LAB.* code), or empty. */
	std::string error;
};

struct Refused {
	/** `sign_in`, `approve` or `keep`. */
	std::string request;
	std::string error_code;
};
using UploaderEvent = std::variant<State, Refused>;

Result<std::string> encode(const UiRequest &request);
Result<std::string> encode(const UploaderEvent &event);
/** VOICE_LAB.UPLOADER_PROTOCOL_INVALID for anything but one whole, valid frame. */
Result<UiRequest> decode_ui_request(std::string_view frame);
Result<UploaderEvent> decode_uploader_event(std::string_view frame);

/**
 * @canonical voice_lab_uploader_client -- the UI's side of one uploader connection.
 *
 * Connects to the uploader's socket and checks that the peer is the exact
 * uploader UID. It sends requests and returns events in order; a malformed
 * event or a lost uploader closes it (VOICE_LAB.UPLOADER_UNAVAILABLE).
 */
class UploaderClient final
{
public:
	static Result<std::unique_ptr<UploaderClient>> connect(const std::string &path, std::uint32_t uploader_uid);
	/** Test seam: an already-connected stream socket; the peer UID is still checked. */
	static Result<std::unique_ptr<UploaderClient>> for_connected_peer(int descriptor, std::uint32_t uploader_uid);
	~UploaderClient();
	UploaderClient(const UploaderClient &) = delete;
	UploaderClient &operator=(const UploaderClient &) = delete;
	Result<bool> send(const UiRequest &request);
	Result<std::vector<UploaderEvent>> poll();
	bool connected() const;

private:
	struct Channel;
	explicit UploaderClient(int descriptor);
	std::unique_ptr<Channel> channel_;
};
} // namespace voice_lab_recorder::uploader_protocol
