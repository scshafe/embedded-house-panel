#include "vocabulary_panel/study_worker.h"
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
using namespace vocabulary_panel;
class WaitingClient final : public LexinetClient
{
      public:
	std::atomic<bool> started{false}, cancelled{false};
	VocabularyPanelResult<StudySession>
	create_or_resume(const StudyCreateRequest &, const std::string &,
			 bhp::http_runtime::CancellationToken token) override
	{
		started = true;
		while (!token.cancelled())
			std::this_thread::yield();
		cancelled = true;
		return VocabularyPanelResult<StudySession>::failure(
		    {"LEXINET.CANCELLED", "Fixture wait cancelled", {}, false});
	}
	VocabularyPanelResult<StudySyncResponse> sync(const StudySyncRequest &, const std::string &,
						      bhp::http_runtime::CancellationToken) override
	{
		return VocabularyPanelResult<StudySyncResponse>::failure(
		    {"TEST.UNUSED", "unused", {}, false});
	}
};
int main()
{
	int failures = 0;
	auto check = [&](bool v, const char *s) {
		if (!v) {
			++failures;
			std::cerr << s << '\n';
		}
	};
	{
		auto runtime = bhp::http_runtime::HttpRuntime::create().value();
		auto client = std::make_shared<WaitingClient>();
		{
			StudyWorker worker(runtime, client);
			check(worker.create_or_resume({}, "test").has_value(), "work accepted");
			const auto deadline =
			    std::chrono::steady_clock::now() + std::chrono::seconds(2);
			while (!client->started && std::chrono::steady_clock::now() < deadline)
				std::this_thread::yield();
			check(client->started, "work off render thread started");
			worker.cancel();
			const auto cancelled_deadline =
			    std::chrono::steady_clock::now() + std::chrono::seconds(2);
			while (!client->cancelled &&
			       std::chrono::steady_clock::now() < cancelled_deadline)
				std::this_thread::yield();
			check(client->cancelled, "per-operation cancellation interrupts client");
			check(!worker.take_completion(),
			      "cancelled generation cannot publish stale completion");
		}
		check(runtime->shutdown().has_value(),
		      "destructor joins worker before runtime cleanup");
	}
	{
		auto runtime = bhp::http_runtime::HttpRuntime::create().value();
		auto client = std::make_shared<WaitingClient>();
		StudyWorker worker(runtime, client);
		worker.create_or_resume({}, "test");
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
		while (!client->started && std::chrono::steady_clock::now() < deadline)
			std::this_thread::yield();
		check(runtime->shutdown().has_value() && client->cancelled,
		      "shared shutdown cancellation interrupts and joins arbitrary client");
	}
	std::cout << "study_worker: " << failures << " failures\n";
	return failures ? 1 : 0;
}
