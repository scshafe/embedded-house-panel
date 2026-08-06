#pragma once

#include "vocabulary_panel/flash_card.h"
#include "vocabulary_panel/flash_card_stage.h"
#include "vocabulary_panel/review_rating.h"
#include "vocabulary_panel/vocabulary_panel_error.h"

#include <optional>
#include <string>

namespace vocabulary_panel {

struct FlashCardSnapshot {
	FlashCardStage flash_card_stage{FlashCardStage::loading};
	std::optional<FlashCard> flash_card;
	std::optional<ReviewRating> pending_review_rating;
	std::optional<VocabularyPanelError> error;
	bool degraded{false};
	std::string degraded_reason;
};

} // namespace vocabulary_panel

