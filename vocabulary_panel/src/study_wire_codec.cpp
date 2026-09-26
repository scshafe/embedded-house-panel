#include "vocabulary_panel/study_wire_codec.h"
#include <algorithm>
#include <ctime>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
namespace vocabulary_panel
{
namespace
{
using Json = nlohmann::json;
const std::vector<std::string> semantics = {"active_scope_create_or_resume",
					    "immutable_complete_card_projection",
					    "scheduler_provenance",
					    "singular_presented_revealed",
					    "append_only_rated_rating_voided",
					    "durable_client_stream_terminal_receipts",
					    "server_authoritative_cycle_exhaustion",
					    "atomic_predecessor_successor_linking",
					    "predecessor_flush_receipt_barrier",
					    "waiting_for_corpus_convergence",
					    "normalized_hash_idempotency",
					    "late_completed_predecessor_correction"};
void need(bool value)
{
	if (!value)
		throw std::runtime_error("schema");
}
VocabularyPanelError schema_error()
{
	return {"LEXINET.RESPONSE_SCHEMA_INVALID",
		"Lexinet study.v1 resource failed strict validation",
		{},
		false};
}
std::string bounded(const Json &j, std::size_t limit)
{
	need(j.is_string());
	auto s = j.get<std::string>();
	std::size_t count = 0;
	for (unsigned char c : s)
		if ((c & 0xc0) != 0x80)
			++count;
	need(count > 0 && count <= limit);
	return s;
}
std::string uuid(const Json &j)
{
	auto s = bounded(j, 36);
	static const std::regex pattern(
	    "^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$");
	need(std::regex_match(s, pattern));
	return s;
}
void nullable_uuid(const Json &j)
{
	if (!j.is_null())
		static_cast<void>(uuid(j));
}
std::string hash(const Json &j)
{
	auto s = bounded(j, 64);
	need(s.size() == 64 && s.find_first_not_of("0123456789abcdef") == std::string::npos);
	return s;
}
std::uint64_t number(const Json &j, std::uint64_t minimum = 0)
{
	need(j.is_number_integer());
	if (j.is_number_unsigned()) {
		auto v = j.get<std::uint64_t>();
		need(v >= minimum && v <= 9223372036854775807ULL);
		return v;
	}
	auto v = j.get<std::int64_t>();
	need(v >= 0 && static_cast<std::uint64_t>(v) >= minimum);
	return static_cast<std::uint64_t>(v);
}
std::int64_t position(const Json &j)
{
	need(j.is_number_integer());
	auto v = j.get<std::int64_t>();
	need(v >= -1 && v <= 2147483647);
	return v;
}
std::string one_of(const Json &j, std::initializer_list<const char *> options)
{
	auto v = bounded(j, 128);
	need(std::any_of(options.begin(), options.end(), [&](const char *o) { return v == o; }));
	return v;
}
void identity(const Json &j, bool error = false)
{
	need(j.at("protocol_version") == "study.v1" && number(j.at("schema_version")) == 1);
	if (!error)
		need(j.at("required_semantics").get<std::vector<std::string>>() == semantics);
}
std::string timestamp(const Json &j)
{
	auto v = bounded(j, 64);
	need(study_timestamp_seconds(v).has_value());
	return v;
}
void scope(const Json &j)
{
	need(j.at("card_kind") == "idea");
	const auto &topics = j.at("topic_refs");
	need(topics.is_array() && !topics.empty() && topics.size() <= 32);
	std::string prior;
	for (const auto &t : topics) {
		auto s = bounded(t, 128);
		need(prior.empty() || prior < s);
		prior = s;
	}
}
StudySession session(const Json &j)
{
	identity(j);
	StudySession s;
	s.session_id = uuid(j.at("session_id"));
	s.profile_ref = bounded(j.at("profile_ref"), 128);
	scope(j.at("scope"));
	s.topic_refs = j.at("scope").at("topic_refs").get<std::vector<std::string>>();
	s.lifecycle = one_of(j.at("lifecycle"), {"active", "completed", "waiting_for_corpus",
						 "superseded_empty", "abandoned"});
	nullable_uuid(j.at("predecessor_session_id"));
	if (!j.at("predecessor_session_id").is_null())
		s.predecessor_session_id = j.at("predecessor_session_id");
	nullable_uuid(j.at("successor_session_id"));
	hash(j.at("corpus_sha256"));
	s.profile_ingestion_high_water = number(j.at("profile_ingestion_high_water"));
	s.state_version = number(j.at("state_version"), 1);
	s.generated_at = timestamp(j.at("generated_at"));
	const auto &cr = j.at("creation_receipt");
	if (!cr.is_null()) {
		uuid(cr.at("creation_request_id"));
		hash(cr.at("normalized_request_sha256"));
		one_of(cr.at("disposition"),
		       {"created", "resumed", "converged_to_existing", "replayed", "successor"});
		need(uuid(cr.at("canonical_session_id")) == s.session_id);
	}
	const auto &scheduler = j.at("scheduler");
	if (!scheduler.is_null()) {
		bounded(scheduler.at("name"), 128);
		bounded(scheduler.at("version"), 64);
		hash(scheduler.at("configuration_sha256"));
		number(scheduler.at("input_profile_ingestion_high_water"));
		timestamp(scheduler.at("planned_at"));
		bounded(scheduler.at("seed"), 128);
		s.scheduler_json = scheduler.dump();
	} else
		need(s.lifecycle != "active");
	const auto &p = j.at("page");
	auto after = position(p.at("after_position")), next = position(p.at("next_after_position"));
	need(p.at("complete").is_boolean());
	s.page_complete = p.at("complete");
	need(p.at("items").is_array() && p.at("items").size() <= 64);
	std::set<std::string> ids;
	auto last = after;
	for (const auto &item : p.at("items")) {
		FlashCard c;
		c.session_item_id = uuid(item.at("session_item_id"));
		need(ids.insert(c.session_item_id).second);
		c.position = position(item.at("position"));
		need(c.position == last + 1);
		last = c.position;
		need(item.at("card_kind") == "idea");
		c.vocabulary_card_id = uuid(item.at("card_identity"));
		nullable_uuid(item.at("live_source_idea_id"));
		const auto &r = item.at("source_revision");
		need(r.at("source_kind") == "idea");
		c.source_identity = uuid(r.at("source_identity"));
		if (!item.at("live_source_idea_id").is_null())
			need(item.at("live_source_idea_id") == c.source_identity);
		c.content_revision = bounded(r.at("content_revision"), 128);
		c.content_sha256 = hash(r.at("content_sha256"));
		c.word = bounded(item.at("front").at("canonical_name"), 512);
		c.definition = bounded(item.at("back").at("definition"), 16384);
		c.example_sentence = bounded(item.at("back").at("example"), 16384);
		const auto &reason = item.at("selection_reason");
		one_of(reason.at("rating_rank"),
		       {"again", "hard", "unseen_or_unrated", "good", "easy"});
		if (!reason.at("latest_presented_ingestion_ordinal").is_null())
			number(reason.at("latest_presented_ingestion_ordinal"), 1);
		hash(reason.at("tie_breaker_sha256"));
		one_of(reason.at("boundary_adjustment"),
		       {"none", "boundary_repeat_rotated", "boundary_repeat_due_to_priority"});
		c.selection_reason_json = reason.dump();
		s.cards.push_back(std::move(c));
	}
	need(next == last);
	if (s.lifecycle == "waiting_for_corpus")
		need(s.cards.empty() && s.page_complete && scheduler.is_null());
	return s;
}
Json parse(const std::string &body)
{
	need(body.size() <= 1048576);
	return Json::parse(body);
}
Json event(const StudyEvent &e)
{
	uuid(e.event_id);
	number(e.client_sequence, 1);
	bounded(e.profile_ref, 128);
	uuid(e.session_item_id);
	uuid(e.card_identity);
	hash(e.content_sha256);
	one_of(e.event_kind, {"presented", "revealed", "rated", "rating_voided"});
	Json j = {{"event_id", e.event_id},
		  {"client_sequence", e.client_sequence},
		  {"profile_ref", e.profile_ref},
		  {"session_item_id", e.session_item_id},
		  {"card_identity", e.card_identity},
		  {"content_sha256", e.content_sha256},
		  {"client_clock_quality", "unknown"},
		  {"client_observed_at", nullptr},
		  {"kind", e.event_kind}};
	if (e.event_kind == "rated")
		j["rating"] = one_of(e.rating, {"again", "hard", "good", "easy"});
	if (e.event_kind == "rating_voided")
		j["rating_event_id"] = uuid(e.target_rating_event_id);
	return j;
}
} // namespace
VocabularyPanelResult<std::int64_t> study_timestamp_seconds(const std::string &value)
{
	using Result = VocabularyPanelResult<std::int64_t>;
	static const std::regex pattern(
	    "^([0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2})(\\.[0-9]+)?Z$");
	std::smatch match;
	if (!std::regex_match(value, match, pattern))
		return Result::failure(schema_error());
	std::tm t{};
	std::istringstream input(match[1].str());
	input >> std::get_time(&t, "%Y-%m-%dT%H:%M:%S");
	if (input.fail())
		return Result::failure(schema_error());
	const auto original = t;
	auto seconds = timegm(&t);
	if (t.tm_year != original.tm_year || t.tm_mon != original.tm_mon ||
	    t.tm_mday != original.tm_mday || t.tm_hour != original.tm_hour ||
	    t.tm_min != original.tm_min || t.tm_sec != original.tm_sec)
		return Result::failure(schema_error());
	return Result::success(static_cast<std::int64_t>(seconds));
}
VocabularyPanelResult<StudySession> decode_study_session(const std::string &body)
{
	try {
		return VocabularyPanelResult<StudySession>::success(session(parse(body)));
	} catch (const std::exception &) {
		return VocabularyPanelResult<StudySession>::failure(schema_error());
	}
}
VocabularyPanelResult<StudySyncResponse> decode_study_sync(const std::string &body)
{
	using Result = VocabularyPanelResult<StudySyncResponse>;
	try {
		const auto j = parse(body);
		identity(j);
		StudySyncResponse r;
		r.sync_id = uuid(j.at("sync_id"));
		hash(j.at("normalized_request_sha256"));
		r.client_stream_id = uuid(j.at("client_stream_id"));
		r.adjudicated_through_sequence = number(j.at("adjudicated_through_sequence"));
		r.profile_ingestion_high_water = number(j.at("profile_ingestion_high_water"));
		timestamp(j.at("generated_at"));
		r.session = session(j.at("session"));
		need(r.profile_ingestion_high_water == r.session.profile_ingestion_high_water);
		need(j.at("receipts").is_array() && j.at("receipts").size() <= 64);
		std::uint64_t last = 0;
		for (const auto &v : j.at("receipts")) {
			StudyEventReceipt e;
			e.client_sequence = number(v.at("client_sequence"), 1);
			need(last == 0 || e.client_sequence == last + 1);
			last = e.client_sequence;
			e.event_id = uuid(v.at("event_id"));
			hash(v.at("payload_sha256"));
			e.disposition =
			    one_of(v.at("disposition"),
				   {"accepted", "semantic_duplicate", "terminal_rejected"});
			if (e.disposition == "terminal_rejected") {
				e.code = one_of(v.at("code"), {"STUDY_EVENT_TRANSITION_INVALID",
							       "STUDY_EVENT_ID_CONFLICT",
							       "STUDY_RATING_ALREADY_EFFECTIVE",
							       "STUDY_RATING_VOID_TARGET_INVALID",
							       "STUDY_CONTENT_REVISION_MISMATCH"});
				need(v.at("canonical_event_id").is_null() &&
				     v.at("profile_ingestion_ordinal").is_null());
			} else {
				e.code = bounded(v.at("code"), 128);
				need(e.code == (e.disposition == "accepted"
						    ? "STUDY_EVENT_ACCEPTED"
						    : "STUDY_EVENT_SEMANTIC_DUPLICATE"));
				e.canonical_event_id = uuid(v.at("canonical_event_id"));
				e.profile_ingestion_ordinal =
				    number(v.at("profile_ingestion_ordinal"), 1);
				need(*e.profile_ingestion_ordinal <=
				     r.profile_ingestion_high_water);
			}
			r.receipts.push_back(std::move(e));
		}
		need(r.receipts.empty() || last == r.adjudicated_through_sequence);
		const auto &f = j.at("flush_receipt");
		if (!f.is_null()) {
			uuid(f.at("receipt_id"));
			need(uuid(f.at("session_id")) == r.session.session_id &&
			     uuid(f.at("sync_id")) == r.sync_id &&
			     uuid(f.at("client_stream_id")) == r.client_stream_id);
			need(number(f.at("acknowledged_through_sequence")) ==
			     r.adjudicated_through_sequence);
			need(number(f.at("profile_ingestion_high_water")) <=
			     r.profile_ingestion_high_water);
			number(f.at("state_version"), 1);
			timestamp(f.at("issued_at"));
			r.flush_receipt_json = f.dump();
		}
		return Result::success(std::move(r));
	} catch (const std::exception &) {
		return Result::failure(schema_error());
	}
}
VocabularyPanelResult<std::string> encode_study_create(const StudyCreateRequest &r)
{
	using Result = VocabularyPanelResult<std::string>;
	try {
		uuid(r.creation_request_id);
		bounded(r.profile_ref, 128);
		Json sc = {{"topic_refs", r.topic_refs}, {"card_kind", "idea"}};
		scope(sc);
		need(r.limit > 0 && r.limit <= 64);
		Json j = {{"protocol_version", "study.v1"},
			  {"schema_version", 1},
			  {"creation_request_id", r.creation_request_id},
			  {"profile_ref", r.profile_ref},
			  {"scope", sc},
			  {"page", {{"after_position", -1}, {"limit", r.limit}}},
			  {"predecessor", nullptr}};
		if (!r.predecessor_session_id.empty()) {
			const auto receipt = parse(r.predecessor_flush_receipt_json);
			need(uuid(receipt.at("session_id")) == uuid(r.predecessor_session_id));
			j["predecessor"] = {{"session_id", r.predecessor_session_id},
					    {"flush_receipt_id", uuid(receipt.at("receipt_id"))}};
		}
		return Result::success(j.dump());
	} catch (const std::exception &) {
		return Result::failure({"LEXINET.REQUEST_SCHEMA_INVALID",
					"Invalid study session creation request",
					{},
					false});
	}
}
VocabularyPanelResult<std::string> encode_study_sync(const StudySyncRequest &r)
{
	using Result = VocabularyPanelResult<std::string>;
	try {
		uuid(r.sync_id);
		uuid(r.session_id);
		uuid(r.client_stream_id);
		need(r.events.size() <= 64 && r.limit > 0 && r.limit <= 64 &&
		     r.expected_state_version > 0);
		position(r.after_position);
		Json events = Json::array();
		std::uint64_t last = 0;
		for (const auto &e : r.events) {
			need(e.session_id == r.session_id &&
			     (last == 0 || e.client_sequence == last + 1));
			last = e.client_sequence;
			events.push_back(event(e));
		}
		Json j = {{"protocol_version", "study.v1"},
			  {"schema_version", 1},
			  {"sync_id", r.sync_id},
			  {"client_stream_id", r.client_stream_id},
			  {"expected_state_version", r.expected_state_version},
			  {"events", events},
			  {"page", {{"after_position", r.after_position}, {"limit", r.limit}}}};
		auto body = j.dump();
		need(body.size() <= 262144);
		return Result::success(std::move(body));
	} catch (const std::exception &) {
		return Result::failure({"LEXINET.REQUEST_SCHEMA_INVALID",
					"Invalid study synchronization request",
					{},
					false});
	}
}
VocabularyPanelError decode_study_error(const std::string &body, long status)
{
	try {
		const auto j = parse(body).at("error");
		identity(j, true);
		const auto code = one_of(
		    j.at("code"),
		    {"STUDY_SCHEMA_INVALID", "UNAUTHORIZED", "STUDY_AUTHORIZATION_DENIED",
		     "STUDY_PROFILE_NOT_FOUND", "STUDY_SESSION_NOT_FOUND", "IDEMPOTENCY_CONFLICT",
		     "STUDY_CYCLE_NOT_EXHAUSTED", "STUDY_FLUSH_RECEIPT_REQUIRED",
		     "STUDY_FLUSH_RECEIPT_STALE", "STUDY_STATE_STALE", "STUDY_STREAM_SEQUENCE_GAP",
		     "STUDY_REQUEST_TOO_LARGE", "STUDY_RATE_LIMITED", "STUDY_SERVICE_UNAVAILABLE",
		     "INTERNAL"});
		bounded(j.at("message"), 1024);
		if (j.contains("candidates")) {
			need(j.at("candidates").is_array() && j.at("candidates").size() <= 16);
			for (const auto &c : j.at("candidates"))
				bounded(c, 128);
		}
		return {code,
			"Lexinet returned a structured study error",
			{{"status", std::to_string(status)}},
			status == 429 || status >= 500};
	} catch (const std::exception &) {
		return schema_error();
	}
}
} // namespace vocabulary_panel
