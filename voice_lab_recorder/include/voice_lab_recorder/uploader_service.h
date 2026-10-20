#pragma once
#include "voice_lab_recorder/corpus_client.h"
#include "voice_lab_recorder/home_host.h"
#include "voice_lab_recorder/outbox.h"
#include "voice_lab_recorder/pocket_id_sign_in.h"
#include "voice_lab_recorder/uploader_protocol.h"
#include <chrono>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

/**
 * @canonical voice_lab_uploader_service -- what `bhp-voice-lab-uploader` does (wire plan Step 7).
 *
 * The one holder of the voice-lab refresh token (ADR 0012). It answers the
 * UI's requests (uploader protocol, Decision 4) and does the background work
 * in `tick()`:
 * - **Sign-in:** a device-code flow started by the UI, polled at its
 *   interval; the refresh token is saved by `VoiceLabSession`.
 * - **The D-021 approval:** while none is in force, a preview for the
 *   configured endpoint and the node the resolver reports, stable for ten
 *   minutes so the digest the owner reviews is the one confirmed.
 * - **The catalog and the panel's library**, read with their `ETag`s.
 * - **The outbox** (Step 6), one step at a time.
 * Nothing here logs a token; the state the UI gets holds only the device
 * code's URI and user code.
 */
namespace voice_lab_recorder
{
struct UploaderSettings {
	SignInSettings sign_in;
	CorpusClientSettings corpus;
	/** Without a measured channel map (`outbox.channels` empty), keep is VOICE_LAB.CAPTURE_PROFILE_UNMEASURED. */
	OutboxSettings outbox;
	/** Owner-only: `refresh-token.json`. */
	std::string token_directory;
	/** The PC's API host name and its tailnet (configuration; the node comes from the resolver). */
	std::string approval_endpoint;
	std::string tailnet_suffix;
	std::string approved_by{"the owner"};
	std::string statement{"Uploads of my own takes from the house panel to my own PC."};
	std::chrono::hours approval_lifetime{24 * 365};
	std::chrono::seconds catalog_refresh{300};
};

struct UploaderPorts {
	HttpTransport transport;
	PeerResolver resolve;
	std::function<std::string()> utc_now;
	std::function<std::chrono::steady_clock::time_point()> steady_now;
	std::function<double()> random01;
	std::function<Result<std::string>()> request_id;
};

class UploaderService final
{
public:
	static Result<std::unique_ptr<UploaderService>> open(UploaderSettings settings, UploaderPorts ports);
	~UploaderService();

	/** Answers one UI request with the events to send back, in order. */
	std::vector<uploader_protocol::UploaderEvent> handle(const uploader_protocol::UiRequest &request);
	/** One round of background work; how long until it wants another. */
	std::chrono::milliseconds tick();
	/** The state the UI shows; its sequence rises whenever any of it changed. */
	uploader_protocol::State state();

private:
	UploaderService(UploaderSettings settings, UploaderPorts ports);
	Result<bool> ensure_endpoints();
	void refresh_approval();
	void refresh_views();
	std::optional<nlohmann::json> find_prompt(const std::string &prompt_id) const;

	UploaderSettings settings_;
	UploaderPorts ports_;
	RefreshTokenStore store_;
	std::optional<Endpoints> endpoints_;
	std::unique_ptr<VoiceLabSession> session_;
	std::optional<DeviceCodeSignIn> flow_;
	std::string sign_in_state_{"signed_out"};
	std::unique_ptr<CorpusClient> client_;
	std::unique_ptr<Outbox> outbox_;
	HomeHost home_host_;
	std::optional<nlohmann::json> preview_;
	std::chrono::steady_clock::time_point preview_made_{};
	nlohmann::json catalog_;
	std::string catalog_etag_;
	nlohmann::json library_;
	std::string library_etag_;
	std::chrono::steady_clock::time_point views_due_{};
	std::chrono::steady_clock::time_point outbox_due_{};
	std::string error_;
	uploader_protocol::State last_;
};
} // namespace voice_lab_recorder
