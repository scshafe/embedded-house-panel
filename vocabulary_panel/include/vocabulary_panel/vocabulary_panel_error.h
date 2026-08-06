#pragma once

#include <map>
#include <string>

namespace vocabulary_panel {

struct VocabularyPanelError {
	std::string error_code;
	std::string error_message;
	std::map<std::string, std::string> error_context;
	bool retryable{false};
};

} // namespace vocabulary_panel

