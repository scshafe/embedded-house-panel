#pragma once

#include "vocabulary_panel/flash_card.h"
#include "vocabulary_panel/review_command.h"
#include "vocabulary_panel/review_receipt.h"
#include "vocabulary_panel/vocabulary_panel_result.h"

#include <optional>
#include <string>

namespace vocabulary_panel {

/**
 * @extension-point lexinet_client -- authoritative vocabulary persistence.
 *
 * Implementations must distinguish a successful empty response from a failed
 * request and must preserve idempotency keys for review retries.
 */
class LexinetClient {
public:
	virtual ~LexinetClient() = default;

	virtual VocabularyPanelResult<std::optional<FlashCard>>
	fetch_next_card(const std::string &review_cursor,
			const std::string &trace_id) = 0;

	virtual VocabularyPanelResult<ReviewReceipt>
	submit_review(const ReviewCommand &review_command,
		      const std::string &trace_id) = 0;
};

} // namespace vocabulary_panel

