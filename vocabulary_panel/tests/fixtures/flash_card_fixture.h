#pragma once
#include "vocabulary_panel/flash_card.h"
#include <vector>
namespace vocabulary_panel::test_fixture
{
inline FlashCard valid_flash_card()
{
	FlashCard c;
	c.vocabulary_card_id = "33333333-3333-4333-8333-333333333333";
	c.word = "perspicacious";
	c.definition = "Having a ready insight into things.";
	c.example_sentence = "The perspicacious student noticed the pattern.";
	c.session_item_id = "22222222-2222-4222-8222-222222222222";
	c.content_revision = "fixture-revision-1";
	c.content_sha256 = std::string(64, 'd');
	c.source_identity = "44444444-4444-4444-8444-444444444444";
	c.selection_reason_json = "{}";
	return c;
}
inline std::vector<FlashCard> three_cards()
{
	auto a = valid_flash_card(), b = a, c = a;
	b.session_item_id = "22222222-2222-4222-8222-222222222223";
	b.vocabulary_card_id = "33333333-3333-4333-8333-333333333334";
	b.position = 1;
	b.word = "serendipity";
	c.session_item_id = "22222222-2222-4222-8222-222222222224";
	c.vocabulary_card_id = "33333333-3333-4333-8333-333333333335";
	c.position = 2;
	c.word = "equanimity";
	return {a, b, c};
}
inline const char *session_id() { return "11111111-1111-4111-8111-111111111111"; }
} // namespace vocabulary_panel::test_fixture
