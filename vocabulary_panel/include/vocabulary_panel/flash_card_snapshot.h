#pragma once
#include "vocabulary_panel/flash_card.h"
#include "vocabulary_panel/vocabulary_panel_error.h"
#include <cstdint>
#include <optional>
#include <string>
namespace vocabulary_panel
{
enum class StudyAvailability { loading, ready, empty, refill_required, resume_required, error };
enum class CardFace { front, back };
enum class StudyConnectivity { online, offline };
enum class StudyDelivery { none, queued_locally, syncing, saved, rejected };
/** Read-only presentation projection; orthogonal lifecycle/data/evidence state. */
struct FlashCardSnapshot {
	StudyAvailability availability{StudyAvailability::loading};
	CardFace face{CardFace::front};
	StudyConnectivity connectivity{StudyConnectivity::online};
	StudyDelivery delivery{StudyDelivery::none};
	std::optional<FlashCard> flash_card;
	std::optional<VocabularyPanelError> error;
	std::string session_id;
	std::uint64_t render_generation{0};
	std::size_t history_size{0};
	std::size_t pending_event_count{0};
	bool can_flip{false}, can_next{false}, can_back{false}, can_rate{false}, can_undo{false};
	bool provisional{false}, tracking_paused{false}, stale{false}, clock_trusted{false};
	bool durable_storage{false};
};
const char *study_availability_name(StudyAvailability value);
const char *study_delivery_name(StudyDelivery value);
} // namespace vocabulary_panel
