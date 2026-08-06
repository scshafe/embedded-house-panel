#pragma once

#include "vocabulary_panel/http_lexinet_config.h"
#include "vocabulary_panel/lexinet_client.h"

#include <memory>

namespace bhp {
class StructuredLogger;
}

namespace vocabulary_panel {

/**
 * Provisional HTTP adapter for docs/interfaces/lexinet-contract.md.
 * Do not deploy until the real Lexinet contract has been reconciled.
 */
class HttpLexinetClient final : public LexinetClient {
public:
	HttpLexinetClient(HttpLexinetConfig config, bhp::StructuredLogger &logger);
	~HttpLexinetClient() override;

	HttpLexinetClient(const HttpLexinetClient &) = delete;
	HttpLexinetClient &operator=(const HttpLexinetClient &) = delete;

	VocabularyPanelResult<std::optional<FlashCard>>
	fetch_next_card(const std::string &review_cursor,
			const std::string &trace_id) override;

	VocabularyPanelResult<ReviewReceipt>
	submit_review(const ReviewCommand &review_command,
		      const std::string &trace_id) override;

private:
	struct Implementation;
	std::unique_ptr<Implementation> implementation_;
};

} // namespace vocabulary_panel

