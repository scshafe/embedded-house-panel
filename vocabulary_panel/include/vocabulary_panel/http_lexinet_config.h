#pragma once

#include <cstddef>
#include <string>

namespace vocabulary_panel {

struct HttpLexinetConfig {
	std::string lexinet_base_url;
	std::string next_card_path;
	std::string review_path;
	std::string credential_file_path;
	long request_timeout_ms{5000};
	std::size_t maximum_response_bytes{262144};
	bool allow_insecure_http_for_tests{false};
};

} // namespace vocabulary_panel

