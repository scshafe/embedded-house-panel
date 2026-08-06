#pragma once

#include "vocabulary_panel/lexinet_client.h"

#include <cstddef>
#include <vector>

namespace vocabulary_panel {

class FixtureLexinetClient final : public LexinetClient {
public:
	explicit FixtureLexinetClient(std::vector<FlashCard> flash_cards);

	VocabularyPanelResult<std::optional<FlashCard>>
	fetch_next_card(const std::string &review_cursor,
			const std::string &trace_id) override;

	VocabularyPanelResult<ReviewReceipt>
	submit_review(const ReviewCommand &review_command,
		      const std::string &trace_id) override;

private:
	std::vector<FlashCard> flash_cards_;
	std::size_t next_flash_card_index_{0};
};

} // namespace vocabulary_panel

