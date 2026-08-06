#pragma once

#include "vocabulary_panel/flash_card.h"

namespace vocabulary_panel::test_fixture {

inline FlashCard valid_flash_card()
{
	return {"fixture-card-1", "perspicacious", "Having a ready insight into things.",
		"The perspicacious student noticed the pattern.", "fixture-cursor-1"};
}

} // namespace vocabulary_panel::test_fixture

