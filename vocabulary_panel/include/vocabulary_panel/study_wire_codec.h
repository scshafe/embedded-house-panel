#pragma once
#include "vocabulary_panel/study_models.h"
#include "vocabulary_panel/vocabulary_panel_result.h"
namespace vocabulary_panel
{
/** Strict study.v1 consumer validation; optional response extensions remain permitted. */
VocabularyPanelResult<StudySession> decode_study_session(const std::string &body);
VocabularyPanelResult<StudySyncResponse> decode_study_sync(const std::string &body);
VocabularyPanelResult<std::string> encode_study_create(const StudyCreateRequest &request);
VocabularyPanelResult<std::string> encode_study_sync(const StudySyncRequest &request);
VocabularyPanelError decode_study_error(const std::string &body, long status);
/** UTC RFC3339 second count, or explicit schema failure. */
VocabularyPanelResult<std::int64_t> study_timestamp_seconds(const std::string &value);
} // namespace vocabulary_panel
