#pragma once
#include "bhp/durable_store/durable_store.h"
#include "bug_reporter/report_bundle.h"
#include "bug_reporter/result.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

/**
 * The panel's queue of reports waiting for the PC's inbox, and the rate limits
 * (bug-reports-and-self-repair.md, sections 3 and 8).
 */
namespace bug_reporter
{
struct OutboxLimits {
	std::uint64_t maximum_bytes{32ULL * 1024 * 1024};
	std::size_t maximum_reports{50};
	/** Reports the inbox already holds, kept for the status list. */
	std::size_t status_history{20};
};

struct RateLimits {
	std::int64_t fingerprint_window_seconds{24 * 3600};
	unsigned full_per_fingerprint_per_week{3};
	unsigned automatic_per_hour{3};
	unsigned automatic_per_day{10};
	std::int64_t manual_spacing_seconds{60};
	unsigned manual_per_day{20};
};

enum class Admission {
	/** Write and send a full bundle. */
	Full,
	/** A repeat: only its fingerprint's count rises, sent as a small update. */
	CountOnly,
	/** A manual report over its limit; the screen says so. */
	Refused,
};

struct AdmissionResult {
	Admission admission{Admission::Refused};
	std::string refusal_code;
};

/** The most repeats one count update carries: the inbox refuses more (report_inbox validate). */
inline constexpr std::uint64_t kMaximumCountBatch = 10'000;

/** One count update: `batch` names it, so the inbox adds a retried update only once. */
struct CountBatch {
	std::string fingerprint;
	std::string batch;
	std::uint64_t count{0};
};

struct OutboxEntry {
	std::string report_id;
	std::string kind;
	std::string fingerprint;
	std::int64_t created_at{0};
	std::uint64_t bytes{0};
	/**
	 * `queued` (on the panel), `received` (the inbox acknowledged it; the bundle is gone)
	 * or `refused` (the inbox refused it for good, or it is unreadable; the bundle is gone).
	 */
	std::string state{"queued"};
	/**
	 * The inbox's status once received: `received`, `investigating`, `fix_in_pr`, `merged`,
	 * `update_ready`, `installed` or `wont_fix`; `refused` when it never got there.
	 */
	std::string status{"queued"};
	std::optional<std::uint64_t> pr;
};

/**
 * @canonical bug_report_outbox -- bundles waiting for the inbox, and the limits.
 *
 * An owner-only directory: `outbox.json` (a DurableFile, REPORT.STORAGE_*)
 * and one subdirectory per queued bundle. It holds at most `maximum_reports`
 * bundles and `maximum_bytes`. To fit a new one, the oldest automatic bundles
 * go first; a manual report that still cannot fit is REPORT.OUTBOX_FULL,
 * never dropped silently. A bundle is deleted when the inbox acknowledges it.
 */
class ReportOutbox final
{
public:
	static Result<std::unique_ptr<ReportOutbox>> open(std::string directory, OutboxLimits limits = {},
							  RateLimits rates = {});
	/**
	 * Decides a new report at `now` (Unix seconds) and records the decision.
	 * Manual: one a minute and 20 a day (REPORT.RATE_LIMITED, REPORT.DAILY_LIMIT).
	 * Automatic: a full bundle for the first of a fingerprint in 24 h, at most
	 * three a week for one fingerprint, three an hour and ten a day overall;
	 * otherwise CountOnly.
	 */
	Result<AdmissionResult> admit(const std::string &kind, const std::string &fingerprint, std::int64_t now);
	/** Where a new report's bundle is written (it must not exist yet). */
	std::string bundle_directory(const std::string &report_id) const;
	/** Queues a written bundle, evicting the oldest automatic ones to fit. */
	Result<bool> add(const Bundle &bundle, const std::string &kind, std::int64_t created_at);
	/** Queued bundles, oldest first. */
	std::vector<OutboxEntry> queued() const;
	/** The inbox holds it: the bundle is deleted and only its status is kept. */
	Result<bool> mark_received(const std::string &report_id);
	/** It can never be sent: the bundle is deleted and the list shows it as refused. */
	Result<bool> mark_refused(const std::string &report_id);
	Result<bool> set_status(const std::string &report_id, const std::string &status, std::optional<std::uint64_t> pr);
	/** Fingerprints with repeats the inbox has not acknowledged yet, and how many. */
	std::vector<std::pair<std::string, std::uint64_t>> pending_counts() const;
	/**
	 * The count updates to send, one per fingerprint. A fingerprint with repeats and no
	 * update in flight starts one of at most kMaximumCountBatch repeats, named by
	 * `new_batch` and recorded before it is sent; the same update is sent again until
	 * the inbox acknowledges it, and repeats meanwhile wait for the next one.
	 */
	Result<std::vector<CountBatch>> count_batches(const std::function<std::optional<std::string>()> &new_batch);
	/** The inbox took the update, or refused it for good: either way it is done. */
	Result<bool> clear_count(const std::string &fingerprint, const std::string &batch);
	/** The newest `count` reports, queued or received, newest first. */
	std::vector<OutboxEntry> recent(std::size_t count) const;
	~ReportOutbox();

private:
	struct State;
	ReportOutbox(std::string directory, OutboxLimits limits, RateLimits rates);
	Result<bool> commit();
	std::string directory_;
	OutboxLimits limits_;
	RateLimits rates_;
	bhp::durable_store::DurableFile file_;
	std::unique_ptr<State> state_;
};

/** Removes a bundle directory and its files; quiet when it is already gone. */
void remove_bundle_directory(const std::string &directory);
} // namespace bug_reporter
