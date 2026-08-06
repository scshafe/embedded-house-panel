#pragma once

#include "vocabulary_panel/flash_card_snapshot.h"
#include "vocabulary_panel/review_command.h"
#include "vocabulary_panel/review_receipt.h"
#include "vocabulary_panel/vocabulary_panel_result.h"

#include <optional>
#include <string>

namespace vocabulary_panel {

/** @canonical flash_card_session -- all flash-card interaction transitions live here. */
class FlashCardSession final {
public:
	FlashCardSession();

	const FlashCardSnapshot &snapshot() const;

	void begin_loading();
	VocabularyPanelResult<FlashCardSnapshot> accept_loaded_card(FlashCard flash_card);
	void accept_no_card();
	VocabularyPanelResult<FlashCardSnapshot>
	accept_offline_cached_card(FlashCard flash_card,
				   VocabularyPanelError boundary_error);
	void accept_load_failure(VocabularyPanelError boundary_error);

	VocabularyPanelResult<FlashCardSnapshot> reveal_next();
	VocabularyPanelResult<ReviewCommand>
	begin_review(ReviewRating review_rating,
		     const std::string &reviewed_at_utc,
		     const std::string &idempotency_key);
	VocabularyPanelResult<FlashCardSnapshot>
	accept_review_saved(const ReviewReceipt &review_receipt);
	void accept_review_failure(VocabularyPanelError boundary_error);
	VocabularyPanelResult<FlashCardSnapshot> retry();

private:
	static std::optional<VocabularyPanelError> validate_flash_card(const FlashCard &flash_card);
	VocabularyPanelResult<FlashCardSnapshot> invalid_transition(const std::string &operation) const;

	FlashCardSnapshot snapshot_;
};

} // namespace vocabulary_panel

