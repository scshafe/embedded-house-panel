#pragma once
#include <cstdint>
#include <string>
namespace vocabulary_panel
{
enum class StudyIntent {
	flip,
	next,
	back,
	rate_again,
	rate_hard,
	rate_good,
	rate_easy,
	rating_undo,
	resume
};
enum class StudyInputSource { touch, voice };
/** Shell-authorized exact active-item binding. Never a caller-selected correction UUID. */
struct StudyIntentBinding {
	std::string session_id;
	std::string session_item_id;
	std::string content_revision;
	std::uint64_t render_generation{0};
};
} // namespace vocabulary_panel
