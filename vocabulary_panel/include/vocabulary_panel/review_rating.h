#pragma once

namespace vocabulary_panel {

enum class ReviewRating {
	again,
	hard,
	good,
	easy,
};

const char *review_rating_name(ReviewRating review_rating);

} // namespace vocabulary_panel

