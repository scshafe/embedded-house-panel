#include "fixtures/flash_card_fixture.h"
#include "vocabulary_panel/flash_card_session.h"

#include <iostream>
#include <string>

namespace {

int failures = 0;

void expect(bool condition, const std::string &scenario)
{
	if (condition)
		return;
	std::cerr << "FAILED: " << scenario << '\n';
	++failures;
}

void test_valid_card_starts_at_word()
{
	vocabulary_panel::FlashCardSession session;
	const auto result = session.accept_loaded_card(vocabulary_panel::test_fixture::valid_flash_card());
	expect(result.has_value() && result.value().flash_card_stage == vocabulary_panel::FlashCardStage::word,
	       "valid card starts at word");
}

void test_reveal_sequence_reaches_rating()
{
	vocabulary_panel::FlashCardSession session;
	session.accept_loaded_card(vocabulary_panel::test_fixture::valid_flash_card());
	session.reveal_next();
	session.reveal_next();
	const auto result = session.reveal_next();
	expect(result.has_value() && result.value().flash_card_stage == vocabulary_panel::FlashCardStage::rating,
	       "word definition example sequence reaches rating");
}

void test_review_command_preserves_idempotency_key()
{
	vocabulary_panel::FlashCardSession session;
	session.accept_loaded_card(vocabulary_panel::test_fixture::valid_flash_card());
	session.reveal_next();
	session.reveal_next();
	session.reveal_next();
	const auto result = session.begin_review(vocabulary_panel::ReviewRating::good,
					 "2026-08-05T00:00:00Z", "review-idempotency-1");
	expect(result.has_value() && result.value().idempotency_key == "review-idempotency-1",
	       "review command preserves idempotency key");
}

void test_review_failure_is_visible_and_retains_card()
{
	vocabulary_panel::FlashCardSession session;
	session.accept_loaded_card(vocabulary_panel::test_fixture::valid_flash_card());
	session.reveal_next();
	session.reveal_next();
	session.reveal_next();
	session.begin_review(vocabulary_panel::ReviewRating::good,
			     "2026-08-05T00:00:00Z", "review-idempotency-1");
	session.accept_review_failure({"LEXINET.UNAVAILABLE", "Lexinet unavailable", {}, true});
	expect(session.snapshot().flash_card_stage == vocabulary_panel::FlashCardStage::error &&
		       session.snapshot().flash_card.has_value(),
	       "review failure is visible and retains card");
}

void test_invalid_card_is_not_empty_success()
{
	vocabulary_panel::FlashCardSession session;
	auto invalid_card = vocabulary_panel::test_fixture::valid_flash_card();
	invalid_card.definition.clear();
	const auto result = session.accept_loaded_card(invalid_card);
	expect(!result.has_value() && result.error().error_code == "LEXINET.CARD_SCHEMA_INVALID",
	       "invalid card is a schema error");
}

void test_offline_cached_card_is_explicitly_degraded()
{
	vocabulary_panel::FlashCardSession session;
	const auto result = session.accept_offline_cached_card(
		vocabulary_panel::test_fixture::valid_flash_card(),
		{"LEXINET.UNAVAILABLE", "Lexinet unavailable", {}, true});
	expect(result.has_value() && result.value().degraded &&
		       result.value().degraded_reason == "LEXINET.UNAVAILABLE",
	       "offline cached card is explicitly degraded");
}

void test_offline_cached_card_cannot_create_review()
{
	vocabulary_panel::FlashCardSession session;
	session.accept_offline_cached_card(
		vocabulary_panel::test_fixture::valid_flash_card(),
		{"LEXINET.UNAVAILABLE", "Lexinet unavailable", {}, true});
	session.reveal_next();
	session.reveal_next();
	session.reveal_next();
	session.reveal_next();
	expect(session.snapshot().flash_card_stage ==
		       vocabulary_panel::FlashCardStage::offline_complete,
	       "offline cached card ends at study-complete instead of rating");
	const auto result = session.begin_review(vocabulary_panel::ReviewRating::good,
					 "2026-08-05T00:00:00Z", "offline-review");
	expect(!result.has_value() &&
		       result.error().error_code == "VOCABULARY_PANEL.OFFLINE_REVIEW_DISABLED",
	       "offline cached card cannot create review command");
}

} // namespace

int main()
{
	test_valid_card_starts_at_word();
	test_reveal_sequence_reaches_rating();
	test_review_command_preserves_idempotency_key();
	test_review_failure_is_visible_and_retains_card();
	test_invalid_card_is_not_empty_success();
	test_offline_cached_card_is_explicitly_degraded();
	test_offline_cached_card_cannot_create_review();
	if (failures != 0)
		return 1;
	std::cout << "vocabulary_panel: 8 assertions passed\n";
	return 0;
}
