#include "vocabulary_panel/fixture_lexinet_client.h"

#include <optional>
#include <utility>

namespace vocabulary_panel {

FixtureLexinetClient::FixtureLexinetClient(std::vector<FlashCard> flash_cards)
	: flash_cards_(std::move(flash_cards))
{
}

VocabularyPanelResult<std::optional<FlashCard>>
FixtureLexinetClient::fetch_next_card(const std::string &, const std::string &)
{
	if (next_flash_card_index_ >= flash_cards_.size()) {
		return VocabularyPanelResult<std::optional<FlashCard>>::success(std::nullopt);
	}
	return VocabularyPanelResult<std::optional<FlashCard>>::success(
		flash_cards_.at(next_flash_card_index_++));
}

VocabularyPanelResult<ReviewReceipt>
FixtureLexinetClient::submit_review(const ReviewCommand &review_command,
				    const std::string &)
{
	if (review_command.vocabulary_card_id.empty() ||
	    review_command.idempotency_key.empty() ||
	    review_command.reviewed_at_utc.empty()) {
		return VocabularyPanelResult<ReviewReceipt>::failure({
			"LEXINET.FIXTURE_REVIEW_INVALID",
			"Fixture review requires card ID, timestamp, and idempotency key",
			{{"operation", "submit_review"}},
			false,
		});
	}
	return VocabularyPanelResult<ReviewReceipt>::success({
		"fixture-review-" + review_command.idempotency_key,
		review_command.vocabulary_card_id,
		review_command.reviewed_at_utc,
	});
}

} // namespace vocabulary_panel

