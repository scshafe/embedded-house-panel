#pragma once

#include <string>

namespace vocabulary_panel {

struct ReviewReceipt {
	std::string vocabulary_review_result_id;
	std::string vocabulary_card_id;
	std::string saved_at_utc;
};

} // namespace vocabulary_panel

