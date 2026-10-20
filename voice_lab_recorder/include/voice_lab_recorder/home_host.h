#pragma once
#include "voice_lab_recorder/result.h"
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>
#include <vector>

/**
 * @canonical voice_lab_home_host -- the D-021 standing approval and its upload ledger.
 *
 * corpus-client.v1 PROTOCOL.md section 2, as voice-lab's `home_host.py` keeps
 * them at the pin. The owner approves the PC once, on the panel's screen: the
 * endpoint, the tailnet, the node's stable id and `tag:voice-lab`, for at most
 * 366 days, after reviewing the record's digest. The approval is written once
 * at 0400 and named, with that digest, by `binding.json`. Before every upload
 * session the uploader checks the node that answers against it, and writes a
 * hash-chained ledger entry, `upload-started`, before the first byte; the
 * session ends with `upload-completed` (naming the receipt) or
 * `upload-refused`. Every record is canonical JSON with one final newline.
 * Layout under the directory: `approvals/<id>.json`, `binding.json`,
 * `ledger/<8-digit sequence>.json`.
 */
namespace voice_lab_recorder
{
inline constexpr unsigned kMaximumApprovalDays = 366;
inline constexpr const char *kHomeHostTag = "tag:voice-lab";

struct ApprovalFields {
	std::string approval_id;
	/** The PC's API host name, lower case, in the tailnet. */
	std::string endpoint;
	std::string tailnet_suffix;
	std::string node_stable_id;
	std::vector<std::string> classes{"capture"};
	/** RFC 3339 UTC. */
	std::string approved_at;
	std::string expires_at;
	std::string approved_by;
	std::string statement;
};

/**
 * The HomeHostApproval record, checked as voice-lab checks it:
 * VOICE_LAB.APPROVAL_INVALID for an endpoint outside the tailnet or not lower
 * case, a malformed node id, unsorted or repeated classes, or an expiry that is
 * not after the approval or is more than 366 days after it.
 */
Result<nlohmann::json> home_host_approval(const ApprovalFields &fields);
/** The SHA-256 of a record's stored bytes (canonical JSON and a newline): what a review names. */
Result<std::string> record_sha256(const nlohmann::json &record);
/** Seconds since the epoch for an RFC 3339 UTC timestamp (`Z`, optional fraction); false when malformed. */
bool parse_utc(const std::string &text, std::int64_t &seconds);

/** The node a tailnet resolver reports for the endpoint (from `tailscaled`). */
struct PeerIdentity {
	std::string node_stable_id;
	std::vector<std::string> tags;
	/** Tailscale's own name for the node, with or without the final dot. */
	std::string dns_name;
};

/**
 * VOICE_LAB.HOME_HOST_IDENTITY_MISMATCH unless the peer is the approved node:
 * its stable id, `tag:voice-lab` among its tags, and its name the endpoint.
 */
Result<bool> check_peer(const nlohmann::json &approval, const PeerIdentity &peer);

struct BoundApproval {
	nlohmann::json approval;
	std::string sha256;
};

struct LedgerEvent {
	/** `upload-started`, `upload-completed` or `upload-refused`. */
	std::string event;
	std::string at;
	std::string session_id;
	std::string manifest_sha256;
	/** For `upload-completed`, and only for it. */
	std::string receipt_sha256;
	/** A refusal's reason, at most 2000 characters. */
	std::string detail;
};

class HomeHost final
{
public:
	/** `directory` is created 0700 when missing; it must be owner-only. */
	explicit HomeHost(std::string directory);

	/**
	 * Stores a reviewed approval (0400, write-once) and binds it.
	 * VOICE_LAB.APPROVAL_DIGEST_MISMATCH when `reviewed_sha256` is not this
	 * record's digest, VOICE_LAB.APPROVAL_NOT_IN_FORCE when it does not stand
	 * at `now`, VOICE_LAB.APPROVAL_EXISTS for an id already used.
	 */
	Result<nlohmann::json> approve(const nlohmann::json &approval, const std::string &reviewed_sha256,
				       const std::string &now);
	/**
	 * The bound approval, checked against its binding and the clock.
	 * VOICE_LAB.APPROVAL_MISSING when nothing is bound,
	 * VOICE_LAB.APPROVAL_TAMPERED when the file is not the bound one or not
	 * canonical, VOICE_LAB.APPROVAL_NOT_IN_FORCE when it does not stand at `now`.
	 */
	Result<BoundApproval> load(const std::string &now) const;
	/** Every entry with its digest, in order. VOICE_LAB.LEDGER_BROKEN for a gap, a broken link or a stray file. */
	Result<std::vector<std::pair<nlohmann::json, std::string>>> entries() const;
	/** Writes the next entry (sequence and link filled in) under an exclusive lock, 0400, write-once. */
	Result<nlohmann::json> append(const BoundApproval &approval, const LedgerEvent &event);

private:
	std::string directory_;
};
} // namespace voice_lab_recorder
