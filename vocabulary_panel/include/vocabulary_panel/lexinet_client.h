#pragma once
#include "bhp/http_runtime/http_runtime.h"
#include "vocabulary_panel/study_models.h"
#include "vocabulary_panel/vocabulary_panel_result.h"
namespace vocabulary_panel
{
/** @extension-point lexinet_client -- typed authoritative study.v1 session/sync. */
class LexinetClient
{
      public:
	virtual ~LexinetClient() = default;
	virtual VocabularyPanelResult<StudySession>
	create_or_resume(const StudyCreateRequest &request, const std::string &trace_id,
			 bhp::http_runtime::CancellationToken cancellation = {}) = 0;
	virtual VocabularyPanelResult<StudySyncResponse>
	sync(const StudySyncRequest &request, const std::string &trace_id,
	     bhp::http_runtime::CancellationToken cancellation = {}) = 0;
};
} // namespace vocabulary_panel
