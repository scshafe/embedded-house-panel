#include "vocabulary_panel/study_synchronizer.h"
#include <algorithm>
#include <utility>
namespace vocabulary_panel
{
namespace
{
bool review_event(const StudyEvent &event)
{
	return event.event_kind == "rated" || event.event_kind == "rating_voided";
}
} // namespace

StudySynchronizer::StudySynchronizer(std::unique_ptr<StudyWorker> worker, std::string profile_ref,
				     std::vector<std::string> topic_refs, std::string trace_prefix,
				     Clock clock, std::chrono::milliseconds pacing)
    : worker_(std::move(worker)), profile_ref_(std::move(profile_ref)),
      topic_refs_(std::move(topic_refs)), trace_prefix_(std::move(trace_prefix)),
      clock_(std::move(clock)), pacing_(pacing)
{
}

StudySynchronizerStep StudySynchronizer::step(FlashCardSession &session, const bool surface_active,
					      const bool clock_trusted)
{
	StudySynchronizerStep result;
	if (auto completion = worker_->take_completion()) {
		in_flight_ = false;
		review_sync_in_flight_ = false;
		const auto record_error = [&](const VocabularyPanelError &error) {
			session.set_boundary_error(error);
			result.error_code = error.error_code;
			terminal_error_ = !error.retryable;
		};
		if (completion->error) {
			record_error(*completion->error);
		} else if (completion->session) {
			const auto previous = session.snapshot().session_id;
			auto applied = session.install_session(*completion->session);
			if (!applied.has_value())
				record_error(applied.error());
			else if (!previous.empty())
				successor_materialized_for_ = previous;
		} else if (completion->synchronization) {
			auto applied = session.accept_sync(*completion->synchronization);
			if (!applied.has_value())
				record_error(applied.error());
		}
		result.completion_applied = true;
		next_attempt_ = clock_() + pacing_;
	}
	if (terminal_error_ || in_flight_ || clock_() < next_attempt_ || !surface_active)
		return result;
	session.set_clock_trusted(clock_trusted);
	if (session.snapshot().session_id.empty()) {
		auto creation = session.prepare_create(profile_ref_, topic_refs_);
		if (creation.has_value()) {
			in_flight_ = worker_->create_or_resume(creation.value(), trace_prefix_ + "-create")
					 .has_value();
			result.request_started = in_flight_;
		} else {
			session.set_boundary_error(creation.error());
			result.error_code = creation.error().error_code;
			result.local_failure_recorded = true;
			terminal_error_ = !creation.error().retryable;
		}
	} else {
		if (session.snapshot().availability == StudyAvailability::refill_required &&
		    successor_materialized_for_ != session.snapshot().session_id) {
			auto successor = session.prepare_successor();
			if (successor.has_value()) {
				in_flight_ = worker_
						 ->create_or_resume(successor.value(),
								    trace_prefix_ + "-successor")
						 .has_value();
				result.request_started = in_flight_;
				next_attempt_ = clock_() + pacing_;
				return result;
			}
		}
		if (session.needs_sync()) {
			auto sync = session.prepare_sync();
			if (sync.has_value()) {
				const bool review = std::any_of(sync.value().events.begin(),
								sync.value().events.end(), review_event);
				in_flight_ =
				    worker_->sync(sync.value(), trace_prefix_ + "-sync").has_value();
				review_sync_in_flight_ = in_flight_ && review;
				result.request_started = in_flight_;
				result.sync_started = in_flight_;
			}
		}
	}
	next_attempt_ = clock_() + pacing_;
	return result;
}

void StudySynchronizer::cancel()
{
	worker_->cancel();
	in_flight_ = false;
	review_sync_in_flight_ = false;
}

void StudySynchronizer::reset()
{
	cancel();
	terminal_error_ = false;
}
} // namespace vocabulary_panel
