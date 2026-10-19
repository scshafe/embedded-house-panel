#include "fixtures/flash_card_fixture.h"
#include "vocabulary_panel/fixture_lexinet_client.h"
#include "vocabulary_panel/study_synchronizer.h"
#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>
using namespace vocabulary_panel;
namespace
{
int failures = 0;
void expect(bool value, const char *label)
{
	if (!value) {
		++failures;
		std::cerr << "FAIL: " << label << '\n';
	}
}
/** Wraps the fixture server: counts calls, can fail once, or block until cancelled. */
class ScriptedClient final : public LexinetClient
{
      public:
	explicit ScriptedClient(std::vector<FlashCard> cards) : fixture_(std::move(cards)) {}
	VocabularyPanelResult<StudySession>
	create_or_resume(const StudyCreateRequest &request, const std::string &trace,
			 bhp::http_runtime::CancellationToken token) override
	{
		++creates;
		{
			std::lock_guard<std::mutex> lock(mutex);
			last_create = request;
		}
		if (auto failure = take_failure(token))
			return VocabularyPanelResult<StudySession>::failure(*failure);
		return fixture_.create_or_resume(request, trace, token);
	}
	VocabularyPanelResult<StudySyncResponse> sync(const StudySyncRequest &request,
						      const std::string &trace,
						      bhp::http_runtime::CancellationToken token) override
	{
		++syncs;
		if (auto failure = take_failure(token))
			return VocabularyPanelResult<StudySyncResponse>::failure(*failure);
		return fixture_.sync(request, trace, token);
	}
	std::optional<VocabularyPanelError> take_failure(bhp::http_runtime::CancellationToken token)
	{
		if (block) {
			while (!token.cancelled())
				std::this_thread::yield();
			return VocabularyPanelError{"LEXINET.CANCELLED", "cancelled", {}, false};
		}
		std::lock_guard<std::mutex> lock(mutex);
		auto failure = std::move(fail_next);
		fail_next.reset();
		return failure;
	}
	std::atomic<int> creates{0}, syncs{0};
	std::atomic<bool> block{false};
	std::mutex mutex;
	std::optional<VocabularyPanelError> fail_next;
	StudyCreateRequest last_create;

      private:
	FixtureLexinetClient fixture_;
};

struct Harness {
	explicit Harness(std::vector<FlashCard> cards = test_fixture::three_cards())
	    : runtime(bhp::http_runtime::HttpRuntime::create().value()),
	      client(std::make_shared<ScriptedClient>(std::move(cards))),
	      synchronizer(std::make_unique<StudyWorker>(runtime, client), "fixture-default",
			   {"fixture-systems", "fixture-words"}, "test", [this] { return now; })
	{
	}
	~Harness() { static_cast<void>(runtime->shutdown()); }
	/** Steps until a completion is applied; real time bounds the worker thread. */
	StudySynchronizerStep finish()
	{
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
		while (std::chrono::steady_clock::now() < deadline) {
			auto result = synchronizer.step(session, true, true);
			if (result.completion_applied)
				return result;
			std::this_thread::yield();
		}
		expect(false, "completion arrives within the deadline");
		return {};
	}
	void advance() { now += std::chrono::seconds(1); }
	std::shared_ptr<bhp::http_runtime::HttpRuntime> runtime;
	std::shared_ptr<ScriptedClient> client;
	std::chrono::steady_clock::time_point now{std::chrono::seconds(1000)};
	FlashCardSession session;
	StudySynchronizer synchronizer;
};

void rate(FlashCardSession &session)
{
	session.apply_intent(StudyIntent::flip, StudyInputSource::touch, session.current_binding());
	session.apply_intent(StudyIntent::rate_good, StudyInputSource::touch,
			     session.current_binding());
}

void test_inactive_surface_sends_nothing()
{
	Harness h;
	const auto result = h.synchronizer.step(h.session, false, true);
	expect(!result.request_started && h.client->creates == 0,
	       "no request while the surface is inactive");
}

void test_creates_then_paces_then_syncs_a_rating()
{
	Harness h;
	const auto started = h.synchronizer.step(h.session, true, true);
	expect(started.request_started && !started.sync_started && h.synchronizer.in_flight(),
	       "creates when there is no session");
	expect(!h.synchronizer.step(h.session, true, true).request_started && h.client->creates <= 1,
	       "one operation in flight");
	const auto created = h.finish();
	expect(!created.error_code && h.session.snapshot().session_id == test_fixture::session_id(),
	       "the created session is installed");
	rate(h.session);
	expect(h.session.needs_sync(), "a rating needs sync");
	expect(!h.synchronizer.step(h.session, true, true).request_started,
	       "pacing holds the next request");
	h.advance();
	const auto syncing = h.synchronizer.step(h.session, true, true);
	expect(syncing.sync_started && h.synchronizer.review_sync_in_flight(),
	       "a rating sync is a review sync");
	h.finish();
	expect(h.session.pending_events().empty() &&
		   h.session.snapshot().delivery == StudyDelivery::saved,
	       "the acknowledged rating leaves the outbox");
}

void test_retryable_failure_retries_after_pacing()
{
	Harness h;
	h.synchronizer.step(h.session, true, true);
	h.finish();
	rate(h.session);
	h.advance();
	{
		std::lock_guard<std::mutex> lock(h.client->mutex);
		h.client->fail_next =
		    VocabularyPanelError{"LEXINET.HTTP_TRANSPORT_FAILED", "offline", {}, true};
	}
	h.synchronizer.step(h.session, true, true);
	const auto failed = h.finish();
	expect(failed.error_code == std::string("LEXINET.HTTP_TRANSPORT_FAILED") &&
		   !h.synchronizer.stopped() && !h.session.pending_events().empty(),
	       "a retryable failure keeps the rating queued");
	h.advance();
	expect(h.synchronizer.step(h.session, true, true).sync_started, "it retries");
	h.finish();
	expect(h.session.pending_events().empty() && h.client->syncs == 2,
	       "the retry delivers the rating");
}

void test_terminal_failure_stops_until_reset()
{
	Harness h;
	{
		std::lock_guard<std::mutex> lock(h.client->mutex);
		h.client->fail_next =
		    VocabularyPanelError{"LEXINET.AUTHORIZATION_FAILED", "denied", {}, false};
	}
	h.synchronizer.step(h.session, true, true);
	h.finish();
	expect(h.synchronizer.stopped(), "a non-retryable error stops the synchronizer");
	h.advance();
	expect(!h.synchronizer.step(h.session, true, true).request_started && h.client->creates == 1,
	       "a stopped synchronizer sends nothing");
	h.synchronizer.reset();
	expect(h.synchronizer.step(h.session, true, true).request_started,
	       "reset allows a new attempt");
	h.finish();
}

void test_cancel_drops_the_in_flight_completion()
{
	Harness h;
	h.client->block = true;
	expect(h.synchronizer.step(h.session, true, true).request_started, "request starts");
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
	while (h.client->creates == 0 && std::chrono::steady_clock::now() < deadline)
		std::this_thread::yield();
	h.synchronizer.cancel();
	expect(!h.synchronizer.in_flight(), "cancel clears the in-flight operation");
	h.client->block = false;
	std::this_thread::sleep_for(std::chrono::milliseconds(50));
	const auto after = h.synchronizer.step(h.session, false, true);
	expect(!after.completion_applied && h.session.snapshot().session_id.empty(),
	       "the cancelled completion is never applied");
}

void test_refill_requests_one_successor_after_the_flush()
{
	Harness h({test_fixture::valid_flash_card()});
	h.synchronizer.step(h.session, true, true);
	h.finish();
	rate(h.session);
	expect(h.session.snapshot().availability == StudyAvailability::refill_required,
	       "the last card needs a refill");
	h.advance();
	h.synchronizer.step(h.session, true, true);
	h.finish();
	expect(h.session.pending_events().empty(), "the predecessor is flushed first");
	h.advance();
	const auto successor = h.synchronizer.step(h.session, true, true);
	h.finish();
	StudyCreateRequest request;
	{
		std::lock_guard<std::mutex> lock(h.client->mutex);
		request = h.client->last_create;
	}
	expect(successor.request_started && h.client->creates == 2 &&
		   request.predecessor_session_id == test_fixture::session_id() &&
		   !request.predecessor_flush_receipt_json.empty(),
	       "a refill creates one successor with the flush receipt");
}
} // namespace

int main()
{
	test_inactive_surface_sends_nothing();
	test_creates_then_paces_then_syncs_a_rating();
	test_retryable_failure_retries_after_pacing();
	test_terminal_failure_stops_until_reset();
	test_cancel_drops_the_in_flight_completion();
	test_refill_requests_one_successor_after_the_flush();
	std::cout << "study_synchronizer: " << failures << " failures\n";
	return failures ? 1 : 0;
}
