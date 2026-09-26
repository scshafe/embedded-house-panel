#include "vocabulary_panel/study_worker.h"
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <variant>
namespace vocabulary_panel
{
struct StudyWorker::State {
	std::mutex mutex;
	std::condition_variable changed;
	std::shared_ptr<LexinetClient> client;
	std::optional<std::variant<StudyCreateRequest, StudySyncRequest>> request;
	std::string trace;
	std::optional<StudyWorkerCompletion> completion;
	bhp::http_runtime::CancellationToken cancellation;
	std::uint64_t generation{0};
	bool busy{false}, closing{false}, finished{false};
};
StudyWorker::StudyWorker(std::shared_ptr<bhp::http_runtime::HttpRuntime> runtime,
			 std::shared_ptr<LexinetClient> client)
    : state_(std::make_shared<State>())
{
	if (!runtime || !client)
		throw std::invalid_argument("STUDY.WORKER_CONFIG_INVALID");
	state_->client = std::move(client);
	const auto s = state_;
	auto registered = runtime->start_worker([s](bhp::http_runtime::CancellationToken global) {
		for (;;) {
			std::unique_lock<std::mutex> lock(s->mutex);
			s->changed.wait_for(lock, std::chrono::milliseconds(10), [&] {
				return s->request || s->closing || global.cancelled();
			});
			if (s->closing || global.cancelled()) {
				s->cancellation.cancel();
				s->finished = true;
				s->changed.notify_all();
				return;
			}
			if (!s->request)
				continue;
			auto request = std::move(*s->request);
			s->request.reset();
			const auto generation = s->generation;
			const auto trace = s->trace;
			const auto cancellation =
			    bhp::http_runtime::CancellationToken::linked(s->cancellation, global);
			lock.unlock();
			StudyWorkerCompletion completion;
			completion.generation = generation;
			try {
				if (std::holds_alternative<StudyCreateRequest>(request)) {
					auto response = s->client->create_or_resume(
					    std::get<StudyCreateRequest>(request), trace,
					    cancellation);
					if (response.has_value())
						completion.session = response.value();
					else
						completion.error = response.error();
				} else {
					auto response =
					    s->client->sync(std::get<StudySyncRequest>(request),
							    trace, cancellation);
					if (response.has_value())
						completion.synchronization = response.value();
					else
						completion.error = response.error();
				}
			} catch (const std::exception &) {
				completion.error = VocabularyPanelError{
				    "STUDY.WORKER_EXCEPTION",
				    "Study worker encountered an internal client failure",
				    {},
				    false};
			}
			lock.lock();
			s->busy = false;
			if (generation == s->generation && !s->closing && !global.cancelled())
				s->completion = std::move(completion);
			s->changed.notify_all();
		}
	});
	if (!registered.has_value())
		throw std::runtime_error(registered.error().code);
}
StudyWorker::~StudyWorker()
{
	std::unique_lock<std::mutex> lock(state_->mutex);
	state_->closing = true;
	state_->cancellation.cancel();
	state_->changed.notify_all();
	state_->changed.wait(lock, [&] { return state_->finished; });
}
VocabularyPanelResult<std::uint64_t> StudyWorker::create_or_resume(StudyCreateRequest r,
								   std::string trace)
{
	std::lock_guard<std::mutex> lock(state_->mutex);
	if (state_->busy || state_->completion || state_->closing)
		return VocabularyPanelResult<std::uint64_t>::failure(
		    {"STUDY.WORKER_BUSY", "Study worker already has unconsumed work", {}, false});
	state_->cancellation = {};
	state_->request = std::move(r);
	state_->trace = std::move(trace);
	state_->busy = true;
	++state_->generation;
	state_->changed.notify_all();
	return VocabularyPanelResult<std::uint64_t>::success(state_->generation);
}
VocabularyPanelResult<std::uint64_t> StudyWorker::sync(StudySyncRequest r, std::string trace)
{
	std::lock_guard<std::mutex> lock(state_->mutex);
	if (state_->busy || state_->completion || state_->closing)
		return VocabularyPanelResult<std::uint64_t>::failure(
		    {"STUDY.WORKER_BUSY", "Study worker already has unconsumed work", {}, false});
	state_->cancellation = {};
	state_->request = std::move(r);
	state_->trace = std::move(trace);
	state_->busy = true;
	++state_->generation;
	state_->changed.notify_all();
	return VocabularyPanelResult<std::uint64_t>::success(state_->generation);
}
std::optional<StudyWorkerCompletion> StudyWorker::take_completion()
{
	std::lock_guard<std::mutex> lock(state_->mutex);
	auto result = std::move(state_->completion);
	state_->completion.reset();
	return result;
}
void StudyWorker::cancel()
{
	std::lock_guard<std::mutex> lock(state_->mutex);
	state_->cancellation.cancel();
	state_->completion.reset();
	if (state_->request) {
		state_->request.reset();
		state_->busy = false;
	}
	++state_->generation;
	state_->changed.notify_all();
}
} // namespace vocabulary_panel
