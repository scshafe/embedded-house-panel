#pragma once

#include <string>

namespace vocabulary_panel {

struct FlashCard {
	std::string vocabulary_card_id;
	std::string word;
	std::string definition;
	std::string example_sentence;
	std::string review_cursor;
};

} // namespace vocabulary_panel

