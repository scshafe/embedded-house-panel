#pragma once
#include "vocabulary_panel/lexinet_client.h"
#include <map>
namespace vocabulary_panel
{
/** Deterministic in-process server fixture; no production scheduler or authority. */
class FixtureLexinetClient final : public LexinetClient
{
      public:
	explicit FixtureLexinetClient(StudySession session);
	explicit FixtureLexinetClient(std::vector<FlashCard> flash_cards);
	VocabularyPanelResult<StudySession>
	create_or_resume(const StudyCreateRequest &, const std::string &,
			 bhp::http_runtime::CancellationToken = {}) override;
	VocabularyPanelResult<StudySyncResponse>
	sync(const StudySyncRequest &, const std::string &,
	     bhp::http_runtime::CancellationToken = {}) override;
	bool offline{false};
	bool terminal_rejection{false};

      private:
	StudySession session_;
	std::map<std::string, StudySyncResponse> replays_;
	std::uint64_t high_water_{0};
};
} // namespace vocabulary_panel
