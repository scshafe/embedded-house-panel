#pragma once
#include <cstdint>
#include <string>
namespace vocabulary_panel
{
/** Immutable, server-issued study.v1 projection; scheduling remains in Lexinet. */
struct FlashCard {
	std::string vocabulary_card_id;
	std::string word;
	std::string definition;
	std::string example_sentence;
	std::string review_cursor;
	std::string session_item_id;
	std::string content_revision;
	std::string content_sha256;
	std::string source_identity;
	std::int64_t position{0};
	std::string selection_reason_json;
};
} // namespace vocabulary_panel
