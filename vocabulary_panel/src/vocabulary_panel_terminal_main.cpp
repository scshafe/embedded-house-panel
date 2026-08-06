#include "bhp/structured_logger.h"
#include "vocabulary_panel/fixture_lexinet_client.h"
#include "vocabulary_panel/flash_card_session.h"

#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {

void render(const vocabulary_panel::FlashCardSnapshot &snapshot)
{
	std::cout << "\nstage=" << vocabulary_panel::flash_card_stage_name(snapshot.flash_card_stage);
	if (snapshot.degraded)
		std::cout << " degraded_reason=" << snapshot.degraded_reason;
	std::cout << '\n';
	if (!snapshot.flash_card.has_value())
		return;
	const auto &card = snapshot.flash_card.value();
	switch (snapshot.flash_card_stage) {
	case vocabulary_panel::FlashCardStage::word:
		std::cout << card.word << "\n[enter: reveal]\n";
		break;
	case vocabulary_panel::FlashCardStage::definition:
		std::cout << card.word << "\n" << card.definition << "\n[enter: example]\n";
		break;
	case vocabulary_panel::FlashCardStage::example:
		std::cout << card.example_sentence << "\n[enter: rate]\n";
		break;
	case vocabulary_panel::FlashCardStage::rating:
		std::cout << "rate: [a]gain [h]ard [g]ood [e]asy\n";
		break;
	default:
		break;
	}
}

} // namespace

int main()
{
	bhp::StructuredLogger logger("vocabulary_panel_terminal", std::cerr);
	vocabulary_panel::FixtureLexinetClient client({
		{"fixture-1", "perspicacious", "Having a ready insight into things.",
		 "The perspicacious student noticed the hidden pattern.", "fixture-cursor-1"},
	});
	vocabulary_panel::FlashCardSession session;

	const auto fetched = client.fetch_next_card("", "terminal-start");
	if (!fetched.has_value()) {
		logger.log_error("lexinet_fetch_failed", "terminal-start",
				 fetched.error().error_code, fetched.error().error_message,
				 "unavailable");
		return 1;
	}
	if (!fetched.value().has_value()) {
		session.accept_no_card();
		render(session.snapshot());
		return 0;
	}
	const auto accepted = session.accept_loaded_card(fetched.value().value());
	if (!accepted.has_value()) {
		logger.log_error("flash_card_rejected", "terminal-start",
				 accepted.error().error_code, accepted.error().error_message,
				 "unavailable");
		return 1;
	}

	std::string input;
	while (std::cin.good()) {
		render(session.snapshot());
		std::getline(std::cin, input);
		if (session.snapshot().flash_card_stage == vocabulary_panel::FlashCardStage::rating) {
			const auto rating = input == "a" ? vocabulary_panel::ReviewRating::again
				: input == "h" ? vocabulary_panel::ReviewRating::hard
				: input == "e" ? vocabulary_panel::ReviewRating::easy
					       : vocabulary_panel::ReviewRating::good;
			const auto command = session.begin_review(
				rating, "2026-08-05T00:00:00Z", "terminal-review-1");
			if (!command.has_value())
				return 1;
			const auto receipt = client.submit_review(command.value(), "terminal-review");
			if (!receipt.has_value())
				return 1;
			if (!session.accept_review_saved(receipt.value()).has_value())
				return 1;
			logger.log(bhp::StructuredLogLevel::info, "fixture_review_saved",
				   "terminal-review", {{"degraded", "false"}});
			return 0;
		}
		if (!session.reveal_next().has_value())
			return 1;
	}
	return 0;
}

