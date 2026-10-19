#pragma once
#include "vocabulary_panel/http_lexinet_config.h"
#include "vocabulary_panel/vocabulary_panel_result.h"
#include <string>
#include <string_view>
#include <vector>
namespace vocabulary_panel
{
/** The live study client's settings, from the BHP_VOCABULARY_PANEL_* keys. */
struct StudyClientConfiguration {
	HttpLexinetConfig http;
	std::string profile_ref;
	/** Sorted and unique, as study.v1 normalizes them. */
	std::vector<std::string> topic_refs;
};
/**
 * @canonical study_client_configuration -- strict reader for vocabulary-panel.env.
 *
 * Reads the BHP_VOCABULARY_PANEL_* lines of a KEY=VALUE environment file. Blank lines,
 * comments and other keys are skipped, so a shared service environment file works too.
 * Every study key is required exactly once; an unknown study key, a placeholder, a
 * non-HTTPS base URL, a stored credential or an out-of-range bound fails with a
 * STUDY_CONFIG.* code naming the key, never the value. The result keeps the production
 * transport policy: no test CA, no plain HTTP, and an untrusted wall clock.
 */
VocabularyPanelResult<StudyClientConfiguration>
parse_study_client_configuration(std::string_view text);
VocabularyPanelResult<StudyClientConfiguration>
read_study_client_configuration(const std::string &path);
} // namespace vocabulary_panel
