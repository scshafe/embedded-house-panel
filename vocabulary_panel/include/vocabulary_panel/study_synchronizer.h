#pragma once
#include "vocabulary_panel/flash_card_session.h"
#include "vocabulary_panel/study_worker.h"
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>
namespace vocabulary_panel
{
/** What one step changed, so the caller knows whether to re-project and render. */
struct StudySynchronizerStep {
	/** A completion (session, sync or error) was applied to the session. */
	bool completion_applied{false};
	/** A local create preparation failed and was recorded. */
	bool local_failure_recorded{false};
	/** A sync request started (review_sync_in_flight() says if it carries a rating). */
	bool sync_started{false};
	/** A create, successor or sync request started. */
	bool request_started{false};
	/** The error code a completion or local failure reported, for acknowledgement. */
	std::optional<std::string> error_code;
};
/**
 * @canonical study_synchronizer -- drives one FlashCardSession through one StudyWorker.
 *
 * One operation in flight; at least one pacing interval between attempts; requests only
 * while the caller reports the study surface active. Without a session it creates; a
 * refill_required session gets one successor; a pending outbox syncs. A non-retryable
 * error stops it until reset(). Polled only on the UI thread.
 */
class StudySynchronizer final
{
      public:
	using Clock = std::function<std::chrono::steady_clock::time_point()>;
	StudySynchronizer(std::unique_ptr<StudyWorker> worker, std::string profile_ref,
			  std::vector<std::string> topic_refs, std::string trace_prefix,
			  Clock clock = [] { return std::chrono::steady_clock::now(); },
			  std::chrono::milliseconds pacing = std::chrono::seconds(1));
	StudySynchronizer(const StudySynchronizer &) = delete;
	StudySynchronizer &operator=(const StudySynchronizer &) = delete;
	/**
	 * Applies a finished operation, then starts the next one if allowed. surface_active
	 * is the caller's gate (study surface, day mode, presence not absent);
	 * clock_trusted is applied to the session before a request.
	 */
	StudySynchronizerStep step(FlashCardSession &session, bool surface_active,
				   bool clock_trusted);
	/** Drops the in-flight operation and its completion; keeps a terminal stop. */
	void cancel();
	/** cancel(), and clears a terminal stop. */
	void reset();
	bool in_flight() const { return in_flight_; }
	bool review_sync_in_flight() const { return in_flight_ && review_sync_in_flight_; }
	bool stopped() const { return terminal_error_; }

      private:
	std::unique_ptr<StudyWorker> worker_;
	std::string profile_ref_;
	std::vector<std::string> topic_refs_;
	std::string trace_prefix_;
	Clock clock_;
	std::chrono::milliseconds pacing_;
	bool in_flight_{false};
	bool review_sync_in_flight_{false};
	bool terminal_error_{false};
	std::string successor_materialized_for_;
	std::chrono::steady_clock::time_point next_attempt_{};
};
} // namespace vocabulary_panel
