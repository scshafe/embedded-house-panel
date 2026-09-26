#pragma once
#include "vocabulary_panel/http_lexinet_config.h"
#include "vocabulary_panel/lexinet_client.h"
#include <memory>
namespace bhp
{
class StructuredLogger;
}
namespace vocabulary_panel
{
/** Certificate-verified, bounded study.v1 client; execute only on the study worker. */
class HttpLexinetClient final : public LexinetClient
{
      public:
	HttpLexinetClient(std::shared_ptr<bhp::http_runtime::HttpRuntime> runtime,
			  HttpLexinetConfig config, bhp::StructuredLogger &logger);
	~HttpLexinetClient() override;
	HttpLexinetClient(const HttpLexinetClient &) = delete;
	HttpLexinetClient &operator=(const HttpLexinetClient &) = delete;
	VocabularyPanelResult<StudySession>
	create_or_resume(const StudyCreateRequest &, const std::string &,
			 bhp::http_runtime::CancellationToken = {}) override;
	VocabularyPanelResult<StudySyncResponse>
	sync(const StudySyncRequest &, const std::string &,
	     bhp::http_runtime::CancellationToken = {}) override;

      private:
	struct Implementation;
	std::unique_ptr<Implementation> implementation_;
};
} // namespace vocabulary_panel
