#pragma once
#include "vocabulary_panel/lexinet_client.h"
#include <memory>
#include <optional>
namespace vocabulary_panel
{
struct StudyWorkerCompletion {
	std::uint64_t generation{0};
	std::optional<StudySession> session;
	std::optional<StudySyncResponse> synchronization;
	std::optional<VocabularyPanelError> error;
};
/** @canonical study_worker -- bounded HTTP mailbox, polled only on the UI thread. */
class StudyWorker final
{
      public:
	StudyWorker(std::shared_ptr<bhp::http_runtime::HttpRuntime> runtime,
		    std::shared_ptr<LexinetClient> client);
	~StudyWorker();
	/** One operation at a time; busy never queues future UI input. */
	VocabularyPanelResult<std::uint64_t> create_or_resume(StudyCreateRequest request,
							      std::string trace_id);
	VocabularyPanelResult<std::uint64_t> sync(StudySyncRequest request, std::string trace_id);
	std::optional<StudyWorkerCompletion> take_completion();
	/** Lifecycle invalidation drops stale completions and interrupts the current HTTP transfer.
	 */
	void cancel();

      private:
	struct State;
	std::shared_ptr<State> state_;
};
} // namespace vocabulary_panel
