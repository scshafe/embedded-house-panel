#pragma once
#include "vocabulary_panel/flash_card.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
namespace vocabulary_panel
{
struct StudySession {
	std::string session_id, profile_ref, lifecycle, scheduler_json, generated_at;
	std::uint64_t state_version{0}, profile_ingestion_high_water{0};
	bool page_complete{true};
	std::vector<FlashCard> cards;
	std::vector<std::string> topic_refs;
	std::string predecessor_session_id;
};
struct StudyEvent {
	std::string event_id;
	std::uint64_t client_sequence{0};
	std::string session_id, session_item_id, content_revision, event_kind;
	std::string rating, target_rating_event_id;
	std::string group_id;
	std::string profile_ref, card_identity, content_sha256;
};
struct StudyEventReceipt {
	std::uint64_t client_sequence{0};
	std::string event_id, disposition, code, canonical_event_id;
	std::optional<std::uint64_t> profile_ingestion_ordinal;
};
struct StudySyncRequest {
	std::string sync_id, session_id, client_stream_id;
	std::vector<StudyEvent> events;
	std::int64_t after_position{-1};
	std::size_t limit{64};
	std::uint64_t expected_state_version{1};
};
struct StudySyncResponse {
	std::string sync_id, client_stream_id, flush_receipt_json;
	std::uint64_t adjudicated_through_sequence{0}, profile_ingestion_high_water{0};
	std::vector<StudyEventReceipt> receipts;
	StudySession session;
};
struct StudyCreateRequest {
	std::string creation_request_id, profile_ref;
	std::vector<std::string> topic_refs;
	std::string predecessor_session_id, predecessor_flush_receipt_json;
	std::size_t limit{64};
};
/** Cryptographically random RFC4122 UUID; throws on system entropy failure. */
std::string new_study_uuid();
} // namespace vocabulary_panel
