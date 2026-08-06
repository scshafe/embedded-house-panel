#pragma once

#include "vocabulary_panel/review_rating.h"

#include <string>

namespace vocabulary_panel {

struct ReviewCommand {
	std::string vocabulary_card_id;
	ReviewRating review_rating;
	std::string reviewed_at_utc;
	std::string idempotency_key;
};

} // namespace vocabulary_panel

