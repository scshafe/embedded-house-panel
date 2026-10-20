#pragma once
#include "bhp/durable_store/durable_store.h"
#include "voice_lab_recorder/corpus_client.h"
#include "voice_lab_recorder/home_host.h"
#include "voice_lab_recorder/result.h"
#include <chrono>
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <string>
#include <vector>

/**
 * @canonical voice_lab_outbox -- the uploader's durable queue of kept takes.
 *
 * Wire plan Step 6, Decision 3. A take the owner kept arrives as a broker
 * hand-off (`<handoff>/<take id>/original.wav` and `capture.json`). `keep`
 * packages it into the outbox (corpus-take/v2, Step 3), records it in
 * `outbox.json` (a DurableFile) and removes the hand-off copy. `step` then
 * moves the oldest unfinished take one action along:
 *
 *   queued -> uploaded -> accepted -> deciding -> kept
 *
 * - Before each upload attempt it loads the D-021 approval, checks that it
 *   covers `capture` and that the node answering is the approved one, and
 *   writes `upload-started` (once per attempt in this process).
 * - `uploaded`: every byte was sent and the session finalized; it reads the
 *   session until it is `accepted` (then `upload-completed`, naming the
 *   receipt's digest) or `refused` (then `upload-refused`, final).
 * - `accepted`: it sends `kept` with a request id saved before the first send
 *   and the take's revision from its history, and polls it to `applied`.
 *
 * Retryable answers back off; a refusal ends that take (`refused`); a session
 * that cannot proceed (signed out, no approval, another node) blocks the
 * queue with a reason the UI shows. After a restart everything resumes from
 * the durable record and the server's own offsets.
 */
namespace voice_lab_recorder
{
/** Resolves the PC's endpoint to the node that answers for it (`tailscaled` on the board). */
using PeerResolver = std::function<Result<PeerIdentity>(const std::string &endpoint)>;

struct OutboxSettings {
	/** The broker's hand-off directory (Decision 3). */
	std::string handoff_directory;
	/** Owner-only: `outbox.json` and the packages. */
	std::string outbox_directory;
	/** Owner-only: the D-021 approval, binding and ledger. */
	std::string home_host_directory;
	/** The board's channel map, room and primary channel (configuration; the array reports none). */
	nlohmann::json channels;
	std::string room;
	unsigned primary_channel{0};
	/** TakeClient: name, version and platform. */
	nlohmann::json client;
	std::string capture_host;
	bhp::durable_store::BackoffPolicy backoff;
	/** How many takes may wait in the outbox. */
	std::size_t maximum_takes{64};
};

struct KeepRequest {
	std::string take_id;
	/** The catalog version's prompt and the catalog reference, as voice-lab served them. */
	nlohmann::json prompt;
	nlohmann::json catalog;
	std::string requested_at;
};

struct OutboxPorts {
	PeerResolver resolve;
	/** RFC 3339 UTC now, for the records. */
	std::function<std::string()> utc_now;
	std::function<std::chrono::steady_clock::time_point()> steady_now;
	std::function<double()> random01;
	std::function<Result<std::string>()> request_id;
};

/** What `step` did, and when it wants to be called again. */
struct OutboxStep {
	enum class Kind { Idle, Progressed, Waiting, Blocked };
	Kind kind{Kind::Idle};
	std::chrono::milliseconds retry_after{0};
	/** For Blocked: VOICE_LAB.SIGN_IN_REQUIRED, VOICE_LAB.APPROVAL_*, VOICE_LAB.HOME_HOST_IDENTITY_MISMATCH. */
	std::string reason;
};

class Outbox final
{
public:
	static Result<std::unique_ptr<Outbox>> open(OutboxSettings settings, CorpusClient &client, OutboxPorts ports);

	/**
	 * Packages a handed-off take and queues it. VOICE_LAB.OUTBOX_FULL past the
	 * limit; VOICE_LAB.TAKE_* from the packager; an already queued take is
	 * answered as it stands.
	 */
	Result<nlohmann::json> keep(const KeepRequest &request);
	/** One action on the oldest unfinished take; never more than one network exchange chain. */
	OutboxStep step();
	/** Every take with its stage and last error, and the queue's blocked reason, for the UI. */
	nlohmann::json status() const;

private:
	Outbox(OutboxSettings settings, CorpusClient &client, OutboxPorts ports);
	Result<bool> save();
	OutboxStep transfer(nlohmann::json &entry);
	OutboxStep await_outcome(nlohmann::json &entry);
	OutboxStep decide(nlohmann::json &entry);
	OutboxStep retry(nlohmann::json &entry, const Error &error);
	/** An upload or intake refusal: `upload-refused` in the ledger, final for this audio. */
	OutboxStep refuse(nlohmann::json &entry, const Error &error);
	OutboxStep decision_refused(nlohmann::json &entry, const Error &error);
	OutboxStep block(const std::string &reason);
	Result<BoundApproval> standing_approval();

	OutboxSettings settings_;
	CorpusClient &client_;
	OutboxPorts ports_;
	bhp::durable_store::DurableFile file_;
	HomeHost home_host_;
	std::unique_ptr<bhp::durable_store::Backoff> backoff_;
	nlohmann::json state_;
	std::set<std::string> started_this_run_;
	std::string blocked_;
	std::chrono::steady_clock::time_point not_before_{};
};
} // namespace voice_lab_recorder
