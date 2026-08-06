#include "vocabulary_panel/flash_card_session.h"

#include <map>
#include <utility>

namespace vocabulary_panel {

const char *review_rating_name(const ReviewRating review_rating)
{
	switch (review_rating) {
	case ReviewRating::again:
		return "again";
	case ReviewRating::hard:
		return "hard";
	case ReviewRating::good:
		return "good";
	case ReviewRating::easy:
		return "easy";
	}
	return "invalid";
}

const char *flash_card_stage_name(const FlashCardStage flash_card_stage)
{
	switch (flash_card_stage) {
	case FlashCardStage::loading:
		return "loading";
	case FlashCardStage::word:
		return "word";
	case FlashCardStage::definition:
		return "definition";
	case FlashCardStage::example:
		return "example";
	case FlashCardStage::rating:
		return "rating";
	case FlashCardStage::submitting:
		return "submitting";
	case FlashCardStage::empty:
		return "empty";
	case FlashCardStage::offline:
		return "offline";
	case FlashCardStage::offline_complete:
		return "offline_complete";
	case FlashCardStage::error:
		return "error";
	}
	return "invalid";
}

FlashCardSession::FlashCardSession() = default;

const FlashCardSnapshot &FlashCardSession::snapshot() const { return snapshot_; }

void FlashCardSession::begin_loading()
{
	snapshot_ = FlashCardSnapshot{};
}

VocabularyPanelResult<FlashCardSnapshot>
FlashCardSession::accept_loaded_card(FlashCard flash_card)
{
	if (snapshot_.flash_card_stage != FlashCardStage::loading)
		return invalid_transition("accept_loaded_card");
	if (const auto error = validate_flash_card(flash_card); error.has_value())
		return VocabularyPanelResult<FlashCardSnapshot>::failure(error.value());

	snapshot_.flash_card = std::move(flash_card);
	snapshot_.flash_card_stage = FlashCardStage::word;
	snapshot_.error.reset();
	snapshot_.degraded = false;
	snapshot_.degraded_reason.clear();
	return VocabularyPanelResult<FlashCardSnapshot>::success(snapshot_);
}

void FlashCardSession::accept_no_card()
{
	snapshot_ = FlashCardSnapshot{};
	snapshot_.flash_card_stage = FlashCardStage::empty;
}

VocabularyPanelResult<FlashCardSnapshot>
FlashCardSession::accept_offline_cached_card(FlashCard flash_card,
					     VocabularyPanelError boundary_error)
{
	if (snapshot_.flash_card_stage != FlashCardStage::loading)
		return invalid_transition("accept_offline_cached_card");
	if (const auto error = validate_flash_card(flash_card); error.has_value())
		return VocabularyPanelResult<FlashCardSnapshot>::failure(error.value());

	snapshot_.flash_card = std::move(flash_card);
	snapshot_.flash_card_stage = FlashCardStage::offline;
	snapshot_.error = std::move(boundary_error);
	snapshot_.degraded = true;
	snapshot_.degraded_reason = snapshot_.error->error_code;
	return VocabularyPanelResult<FlashCardSnapshot>::success(snapshot_);
}

void FlashCardSession::accept_load_failure(VocabularyPanelError boundary_error)
{
	snapshot_ = FlashCardSnapshot{};
	snapshot_.flash_card_stage = FlashCardStage::error;
	snapshot_.error = std::move(boundary_error);
}

VocabularyPanelResult<FlashCardSnapshot> FlashCardSession::reveal_next()
{
	switch (snapshot_.flash_card_stage) {
	case FlashCardStage::word:
		snapshot_.flash_card_stage = FlashCardStage::definition;
		break;
	case FlashCardStage::definition:
		snapshot_.flash_card_stage = FlashCardStage::example;
		break;
	case FlashCardStage::example:
		snapshot_.flash_card_stage = snapshot_.degraded
						   ? FlashCardStage::offline_complete
						   : FlashCardStage::rating;
		break;
	case FlashCardStage::offline:
		// A cached card is study-only; no review write is implied or queued.
		snapshot_.flash_card_stage = FlashCardStage::word;
		break;
	default:
		return invalid_transition("reveal_next");
	}
	return VocabularyPanelResult<FlashCardSnapshot>::success(snapshot_);
}

VocabularyPanelResult<ReviewCommand>
FlashCardSession::begin_review(const ReviewRating review_rating,
			       const std::string &reviewed_at_utc,
			       const std::string &idempotency_key)
{
	if (snapshot_.degraded) {
		return VocabularyPanelResult<ReviewCommand>::failure({
			"VOCABULARY_PANEL.OFFLINE_REVIEW_DISABLED",
			"A cached offline card is study-only and cannot create a review command",
			{{"operation", "begin_review"},
			 {"degraded_reason", snapshot_.degraded_reason}},
			false,
		});
	}
	if (snapshot_.flash_card_stage != FlashCardStage::rating ||
	    !snapshot_.flash_card.has_value()) {
		return VocabularyPanelResult<ReviewCommand>::failure(
			invalid_transition("begin_review").error());
	}
	if (reviewed_at_utc.empty() || idempotency_key.empty()) {
		return VocabularyPanelResult<ReviewCommand>::failure({
			"VOCABULARY_PANEL.REVIEW_METADATA_INVALID",
			"Review timestamp and idempotency key are required",
			{{"operation", "begin_review"}},
			false,
		});
	}

	snapshot_.pending_review_rating = review_rating;
	snapshot_.flash_card_stage = FlashCardStage::submitting;
	return VocabularyPanelResult<ReviewCommand>::success({
		snapshot_.flash_card->vocabulary_card_id,
		review_rating,
		reviewed_at_utc,
		idempotency_key,
	});
}

VocabularyPanelResult<FlashCardSnapshot>
FlashCardSession::accept_review_saved(const ReviewReceipt &review_receipt)
{
	if (snapshot_.flash_card_stage != FlashCardStage::submitting ||
	    !snapshot_.flash_card.has_value()) {
		return invalid_transition("accept_review_saved");
	}
	if (review_receipt.vocabulary_review_result_id.empty() ||
	    review_receipt.vocabulary_card_id != snapshot_.flash_card->vocabulary_card_id ||
	    review_receipt.saved_at_utc.empty()) {
		return VocabularyPanelResult<FlashCardSnapshot>::failure({
			"LEXINET.REVIEW_RECEIPT_INVALID",
			"Lexinet review receipt did not match the pending card",
			{{"operation", "accept_review_saved"}},
			false,
		});
	}
	begin_loading();
	return VocabularyPanelResult<FlashCardSnapshot>::success(snapshot_);
}

void FlashCardSession::accept_review_failure(VocabularyPanelError boundary_error)
{
	snapshot_.flash_card_stage = FlashCardStage::error;
	snapshot_.error = std::move(boundary_error);
}

VocabularyPanelResult<FlashCardSnapshot> FlashCardSession::retry()
{
	if (snapshot_.flash_card_stage != FlashCardStage::error &&
	    snapshot_.flash_card_stage != FlashCardStage::empty &&
	    snapshot_.flash_card_stage != FlashCardStage::offline &&
	    snapshot_.flash_card_stage != FlashCardStage::offline_complete) {
		return invalid_transition("retry");
	}
	begin_loading();
	return VocabularyPanelResult<FlashCardSnapshot>::success(snapshot_);
}

std::optional<VocabularyPanelError>
FlashCardSession::validate_flash_card(const FlashCard &flash_card)
{
	if (flash_card.vocabulary_card_id.empty() || flash_card.word.empty() ||
	    flash_card.definition.empty() || flash_card.example_sentence.empty()) {
		return VocabularyPanelError{
			"LEXINET.CARD_SCHEMA_INVALID",
			"Card ID, word, definition, and example sentence are required",
			{{"operation", "validate_flash_card"}},
			false,
		};
	}
	return std::nullopt;
}

VocabularyPanelResult<FlashCardSnapshot>
FlashCardSession::invalid_transition(const std::string &operation) const
{
	return VocabularyPanelResult<FlashCardSnapshot>::failure({
		"VOCABULARY_PANEL.TRANSITION_INVALID",
		"The requested flash-card transition is not valid in the current stage",
		{{"operation", operation},
		 {"flash_card_stage", flash_card_stage_name(snapshot_.flash_card_stage)}},
		false,
	});
}

} // namespace vocabulary_panel
