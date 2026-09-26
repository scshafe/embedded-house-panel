#include "vocabulary_panel/fixture_lexinet_client.h"
#include <nlohmann/json.hpp>
namespace vocabulary_panel
{
FixtureLexinetClient::FixtureLexinetClient(StudySession session)
    : session_(std::move(session)), high_water_(session_.profile_ingestion_high_water)
{
}
FixtureLexinetClient::FixtureLexinetClient(std::vector<FlashCard> cards)
{
	session_.session_id = "11111111-1111-4111-8111-111111111111";
	session_.profile_ref = "fixture-default";
	session_.topic_refs = {"fixture-systems", "fixture-words"};
	session_.lifecycle = cards.empty() ? "waiting_for_corpus" : "active";
	session_.state_version = 1;
	session_.cards = std::move(cards);
}
VocabularyPanelResult<StudySession>
FixtureLexinetClient::create_or_resume(const StudyCreateRequest &, const std::string &,
				       bhp::http_runtime::CancellationToken cancel)
{
	if (cancel.cancelled())
		return VocabularyPanelResult<StudySession>::failure(
		    {"LEXINET.CANCELLED", "Fixture request cancelled", {}, false});
	if (offline)
		return VocabularyPanelResult<StudySession>::failure(
		    {"LEXINET.HTTP_TRANSPORT_FAILED", "Fixture offline", {}, true});
	return VocabularyPanelResult<StudySession>::success(session_);
}
VocabularyPanelResult<StudySyncResponse>
FixtureLexinetClient::sync(const StudySyncRequest &request, const std::string &,
			   bhp::http_runtime::CancellationToken cancel)
{
	using Result = VocabularyPanelResult<StudySyncResponse>;
	if (cancel.cancelled())
		return Result::failure(
		    {"LEXINET.CANCELLED", "Fixture request cancelled", {}, false});
	if (offline)
		return Result::failure(
		    {"LEXINET.HTTP_TRANSPORT_FAILED", "Fixture offline", {}, true});
	if (replays_.count(request.sync_id))
		return Result::success(replays_.at(request.sync_id));
	StudySyncResponse response;
	response.sync_id = request.sync_id;
	response.client_stream_id = request.client_stream_id;
	for (const auto &e : request.events) {
		StudyEventReceipt receipt;
		receipt.event_id = e.event_id;
		receipt.client_sequence = e.client_sequence;
		receipt.disposition = terminal_rejection ? "terminal_rejected" : "accepted";
		receipt.code =
		    terminal_rejection ? "STUDY_EVENT_TRANSITION_INVALID" : "STUDY_EVENT_ACCEPTED";
		if (!terminal_rejection) {
			receipt.canonical_event_id = e.event_id;
			receipt.profile_ingestion_ordinal = ++high_water_;
		}
		response.receipts.push_back(std::move(receipt));
		response.adjudicated_through_sequence = e.client_sequence;
	}
	++session_.state_version;
	session_.profile_ingestion_high_water = high_water_;
	response.profile_ingestion_high_water = high_water_;
	response.session = session_;
	response.session.cards.clear();
	response.flush_receipt_json =
	    nlohmann::json(
		{{"receipt_id", new_study_uuid()},
		 {"session_id", session_.session_id},
		 {"sync_id", request.sync_id},
		 {"client_stream_id", request.client_stream_id},
		 {"acknowledged_through_sequence", response.adjudicated_through_sequence},
		 {"profile_ingestion_high_water", high_water_},
		 {"state_version", session_.state_version},
		 {"issued_at", "2026-09-25T12:00:00Z"}})
		.dump();
	replays_[request.sync_id] = response;
	return Result::success(std::move(response));
}
} // namespace vocabulary_panel
