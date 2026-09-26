#pragma once
#include "vocabulary_panel/flash_card_snapshot.h"
#include "vocabulary_panel/study_intent.h"
#include "vocabulary_panel/study_models.h"
#include "vocabulary_panel/study_store.h"
#include <memory>
namespace vocabulary_panel
{
struct StudyBounds {
	std::size_t maximum_cards{64};
	std::size_t maximum_history{32};
	std::size_t maximum_pending_events{64};
	std::size_t maximum_state_bytes{4194304};
	std::size_t prefetch_low_water{4};
	std::uint64_t maximum_offline_seconds{604800};
};
/** @canonical flash_card_session -- single source-qualified study reducer and outbox. */
class FlashCardSession final
{
      public:
	/** Explicit store for durable operation. Default memory store is fixture-only. */
	explicit FlashCardSession(
	    std::shared_ptr<StudyStore> store = std::make_shared<MemoryStudyStore>(),
	    StudyBounds bounds = {});
	~FlashCardSession();
	FlashCardSession(const FlashCardSession &) = delete;
	FlashCardSession &operator=(const FlashCardSession &) = delete;
	const FlashCardSnapshot &snapshot() const;
	StudyIntentBinding current_binding() const;
	/** Restore durable cache before network; provisional frames restart at a non-card gate. */
	VocabularyPanelResult<FlashCardSnapshot> restore();
	/** Persist initial creation UUID and normalized scope before any HTTPS request. */
	VocabularyPanelResult<StudyCreateRequest>
	prepare_create(const std::string &profile_ref, const std::vector<std::string> &topic_refs);
	/** First non-voice entry creates presented before rendering. */
	VocabularyPanelResult<FlashCardSnapshot> install_session(std::vector<FlashCard> cards,
								 std::string session_id);
	VocabularyPanelResult<FlashCardSnapshot> install_session(StudySession session);
	/** Shell serializes validated intents. Every mutation commits before publication. */
	VocabularyPanelResult<FlashCardSnapshot> apply_intent(StudyIntent intent,
							      StudyInputSource source,
							      const StudyIntentBinding &binding);
	/** Repeated call before response retains exact durable request UUID and event payloads. */
	VocabularyPanelResult<StudySyncRequest> prepare_sync();
	VocabularyPanelResult<FlashCardSnapshot> accept_sync(const StudySyncResponse &response);
	/** Flush barrier result for the server-authoritative successor check. */
	VocabularyPanelResult<std::string> predecessor_flush_receipt() const;
	/** Durable creation identity under the local predecessor flush barrier. */
	VocabularyPanelResult<StudyCreateRequest> prepare_successor();
	VocabularyPanelResult<FlashCardSnapshot> leave_surface();
	void set_connectivity(StudyConnectivity connectivity, bool stale = false);
	void set_clock_trusted(bool trusted);
	void set_boundary_error(VocabularyPanelError error);
	/** Whether outbox delivery, retained predecessor refresh, or low-water refill needs work.
	 */
	bool needs_sync() const;
	const std::vector<StudyEvent> &pending_events() const;
	const std::string &client_stream_id() const;

      private:
	struct Implementation;
	std::unique_ptr<Implementation> implementation_;
};
} // namespace vocabulary_panel
