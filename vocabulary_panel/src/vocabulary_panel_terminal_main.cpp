#include "vocabulary_panel/flash_card_session.h"
#include <iostream>
#include <map>
int main()
{
	using namespace vocabulary_panel;
	FlashCard c;
	c.vocabulary_card_id = "33333333-3333-4333-8333-333333333333";
	c.word = "perspicacious";
	c.definition = "Having a ready insight into things.";
	c.example_sentence = "A perceptive observer noticed the pattern.";
	c.session_item_id = "22222222-2222-4222-8222-222222222222";
	c.content_revision = "fixture-1";
	c.content_sha256 = std::string(64, 'd');
	FlashCardSession session;
	auto loaded = session.install_session({c}, "11111111-1111-4111-8111-111111111111");
	if (!loaded.has_value())
		return 1;
	const std::map<std::string, StudyIntent> commands = {
	    {"flip", StudyIntent::flip},	{"reveal", StudyIntent::flip},
	    {"next", StudyIntent::next},	{"back", StudyIntent::back},
	    {"again", StudyIntent::rate_again}, {"hard", StudyIntent::rate_hard},
	    {"good", StudyIntent::rate_good},	{"easy", StudyIntent::rate_easy},
	    {"undo", StudyIntent::rating_undo}};
	std::string line;
	while (std::getline(std::cin, line) && line != "quit") {
		auto command = commands.find(line);
		if (command == commands.end())
			continue;
		auto result = session.apply_intent(command->second, StudyInputSource::touch,
						   session.current_binding());
		const auto &s = session.snapshot();
		std::cout << "{\"availability\":\"" << study_availability_name(s.availability)
			  << "\",\"delivery\":\"" << study_delivery_name(s.delivery)
			  << "\",\"fixture_memory_only\":true,\"accepted\":"
			  << (result.has_value() ? "true" : "false") << "}\n";
	}
	return 0;
}
