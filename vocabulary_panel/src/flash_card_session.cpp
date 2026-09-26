#include "vocabulary_panel/flash_card_session.h"
#include "vocabulary_panel/study_wire_codec.h"
#include <algorithm>
#include <chrono>
#include <limits>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <utility>
namespace vocabulary_panel
{
namespace
{
using Json = nlohmann::json;
using Result = VocabularyPanelResult<FlashCardSnapshot>;
VocabularyPanelError error(std::string code, std::string message)
{
	return {std::move(code), std::move(message), {}, false};
}
struct Item {
	FlashCard card;
	CardFace face{CardFace::front};
	bool presented{false}, revealed{false}, provisional_presented{false},
	    provisional_revealed{false}, rejected{false};
	std::string effective_rating;
};
struct Undo {
	std::string event_id, item_id, group_id;
};
struct State {
	std::string stream{new_study_uuid()}, session, profile{"fixture-default"}, scheduler,
	    generated_at, flush;
	std::uint64_t next_sequence{1}, generation{0}, state_version{1}, high_water{0},
	    cached_at{0};
	std::vector<Item> items;
	std::vector<std::size_t> history;
	std::size_t cursor{0}, next_item{0};
	std::vector<StudyEvent> pending;
	std::optional<Undo> undo;
	std::optional<StudySyncRequest> in_flight;
	std::optional<StudySyncRequest> last_completed_sync;
	bool predecessor_refresh_required{false};
	std::vector<std::string> topics{"fixture-systems", "fixture-words"};
	std::optional<StudyCreateRequest> initial_request;
	std::optional<StudyCreateRequest> successor_request;
	std::optional<StudySession> staged_successor;
	std::vector<std::pair<std::string, std::string>> rejected_receipts;
	StudyAvailability availability{StudyAvailability::loading};
	StudyDelivery delivery{StudyDelivery::none};
	bool tracking_paused{false}, page_complete{true};
};
std::uint64_t now_seconds()
{
	return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
					      std::chrono::system_clock::now().time_since_epoch())
					      .count());
}
Json card_json(const FlashCard &c)
{
	return {{"vocabulary_card_id", c.vocabulary_card_id},
		{"word", c.word},
		{"definition", c.definition},
		{"example_sentence", c.example_sentence},
		{"review_cursor", c.review_cursor},
		{"session_item_id", c.session_item_id},
		{"content_revision", c.content_revision},
		{"content_sha256", c.content_sha256},
		{"source_identity", c.source_identity},
		{"position", c.position},
		{"selection_reason_json", c.selection_reason_json}};
}
FlashCard card_from(const Json &j)
{
	return {j.at("vocabulary_card_id"),   j.at("word"),
		j.at("definition"),	      j.at("example_sentence"),
		j.at("review_cursor"),	      j.at("session_item_id"),
		j.at("content_revision"),     j.at("content_sha256"),
		j.at("source_identity"),      j.at("position"),
		j.at("selection_reason_json")};
}
Json event_json(const StudyEvent &e)
{
	return {{"event_id", e.event_id},
		{"client_sequence", e.client_sequence},
		{"session_id", e.session_id},
		{"session_item_id", e.session_item_id},
		{"content_revision", e.content_revision},
		{"event_kind", e.event_kind},
		{"rating", e.rating},
		{"target_rating_event_id", e.target_rating_event_id},
		{"group_id", e.group_id},
		{"profile_ref", e.profile_ref},
		{"card_identity", e.card_identity},
		{"content_sha256", e.content_sha256}};
}
StudyEvent event_from(const Json &j)
{
	return {j.at("event_id"),	 j.at("client_sequence"),	 j.at("session_id"),
		j.at("session_item_id"), j.at("content_revision"),	 j.at("event_kind"),
		j.at("rating"),		 j.at("target_rating_event_id"), j.at("group_id"),
		j.at("profile_ref"),	 j.at("card_identity"),		 j.at("content_sha256")};
}
Item *active(State &s)
{
	return s.availability == StudyAvailability::ready && !s.history.empty()
		   ? &s.items.at(s.history.at(s.cursor))
		   : nullptr;
}
Json local_session_json(const StudySession &s)
{
	Json cards = Json::array();
	for (const auto &card : s.cards)
		cards.push_back(card_json(card));
	return {{"session_id", s.session_id},
		{"profile_ref", s.profile_ref},
		{"lifecycle", s.lifecycle},
		{"scheduler_json", s.scheduler_json},
		{"generated_at", s.generated_at},
		{"state_version", s.state_version},
		{"profile_ingestion_high_water", s.profile_ingestion_high_water},
		{"page_complete", s.page_complete},
		{"cards", cards},
		{"topic_refs", s.topic_refs},
		{"predecessor_session_id", s.predecessor_session_id}};
}
StudySession local_session_from(const Json &j)
{
	StudySession s;
	s.session_id = j.at("session_id");
	s.profile_ref = j.at("profile_ref");
	s.lifecycle = j.at("lifecycle");
	s.scheduler_json = j.at("scheduler_json");
	s.generated_at = j.at("generated_at");
	s.state_version = j.at("state_version");
	s.profile_ingestion_high_water = j.at("profile_ingestion_high_water");
	s.page_complete = j.at("page_complete");
	s.topic_refs = j.at("topic_refs").get<std::vector<std::string>>();
	s.predecessor_session_id = j.at("predecessor_session_id");
	for (const auto &card : j.at("cards"))
		s.cards.push_back(card_from(card));
	return s;
}
Json local_sync_json(const StudySyncRequest &r)
{
	Json events = Json::array();
	for (const auto &e : r.events)
		events.push_back(event_json(e));
	return {{"sync_id", r.sync_id},
		{"after_position", r.after_position},
		{"expected_state_version", r.expected_state_version},
		{"limit", r.limit},
		{"events", events}};
}
StudySyncRequest local_sync_from(const Json &f, const State &s)
{
	StudySyncRequest r;
	r.sync_id = f.at("sync_id");
	r.session_id = s.session;
	r.client_stream_id = s.stream;
	r.after_position = f.at("after_position");
	r.expected_state_version = f.at("expected_state_version");
	r.limit = f.at("limit");
	for (const auto &e : f.at("events"))
		r.events.push_back(event_from(e));
	return r;
}
Json local_create_json(const StudyCreateRequest &r)
{
	return {{"creation_request_id", r.creation_request_id},
		{"profile_ref", r.profile_ref},
		{"topic_refs", r.topic_refs},
		{"predecessor_session_id", r.predecessor_session_id},
		{"predecessor_flush_receipt_json", r.predecessor_flush_receipt_json},
		{"limit", r.limit}};
}
StudyCreateRequest local_create_from(const Json &j)
{
	StudyCreateRequest r{j.at("creation_request_id"),
			     j.at("profile_ref"),
			     j.at("topic_refs").get<std::vector<std::string>>(),
			     j.at("predecessor_session_id"),
			     j.at("predecessor_flush_receipt_json"),
			     j.at("limit")};
	if (!encode_study_create(r).has_value())
		throw std::runtime_error("create_request_invalid");
	return r;
}
std::string encode(const State &s)
{
	Json j = {{"schema_version", 1},
		  {"stream", s.stream},
		  {"session", s.session},
		  {"profile", s.profile},
		  {"scheduler", s.scheduler},
		  {"generated_at", s.generated_at},
		  {"next_sequence", s.next_sequence},
		  {"generation", s.generation},
		  {"state_version", s.state_version},
		  {"high_water", s.high_water},
		  {"cached_at", s.cached_at},
		  {"flush", s.flush},
		  {"history", s.history},
		  {"cursor", s.cursor},
		  {"next_item", s.next_item},
		  {"availability", static_cast<int>(s.availability)},
		  {"delivery", static_cast<int>(s.delivery)},
		  {"tracking_paused", s.tracking_paused},
		  {"page_complete", s.page_complete},
		  {"rejected_receipts", s.rejected_receipts},
		  {"items", Json::array()},
		  {"pending", Json::array()},
		  {"undo", nullptr},
		  {"in_flight", nullptr},
		  {"topics", s.topics},
		  {"initial_request", nullptr},
		  {"successor_request", nullptr},
		  {"staged_successor", nullptr},
		  {"last_completed_sync", nullptr},
		  {"predecessor_refresh_required", s.predecessor_refresh_required}};
	for (const auto &i : s.items)
		j["items"].push_back({{"card", card_json(i.card)},
				      {"face", static_cast<int>(i.face)},
				      {"presented", i.presented},
				      {"revealed", i.revealed},
				      {"rejected", i.rejected},
				      {"effective_rating", i.effective_rating}});
	for (const auto &e : s.pending)
		j["pending"].push_back(event_json(e));
	if (s.undo)
		j["undo"] = {{"event_id", s.undo->event_id},
			     {"item_id", s.undo->item_id},
			     {"group_id", s.undo->group_id}};
	if (s.in_flight) {
		j["in_flight"] = {{"sync_id", s.in_flight->sync_id},
				  {"after_position", s.in_flight->after_position},
				  {"expected_state_version", s.in_flight->expected_state_version},
				  {"limit", s.in_flight->limit},
				  {"events", Json::array()}};
		for (const auto &e : s.in_flight->events)
			j["in_flight"]["events"].push_back(event_json(e));
	}
	if (s.initial_request)
		j["initial_request"] = local_create_json(*s.initial_request);
	if (s.successor_request)
		j["successor_request"] = local_create_json(*s.successor_request);
	if (s.staged_successor)
		j["staged_successor"] = local_session_json(*s.staged_successor);
	if (s.last_completed_sync)
		j["last_completed_sync"] = local_sync_json(*s.last_completed_sync);
	return j.dump();
}
void validate_card(const FlashCard &c)
{
	if (c.vocabulary_card_id.empty() || c.word.empty() || c.definition.empty() ||
	    c.example_sentence.empty() || c.session_item_id.empty() || c.content_revision.empty() ||
	    c.word.size() > 2048 || c.definition.size() > 65536 ||
	    c.example_sentence.size() > 65536 || c.position < 0)
		throw std::runtime_error("invalid_card");
	// Strict JSON serialization validates UTF-8 even for directly injected fixture cards.
	static_cast<void>(card_json(c).dump());
}
State decode(const std::string &data, const StudyBounds &bounds)
{
	auto j = Json::parse(data);
	if (j.at("schema_version") != 1)
		throw std::runtime_error("schema_version");
	State s;
	s.stream = j.at("stream");
	s.session = j.at("session");
	s.profile = j.at("profile");
	s.scheduler = j.at("scheduler");
	s.generated_at = j.at("generated_at");
	s.next_sequence = j.at("next_sequence");
	s.generation = j.at("generation");
	s.state_version = j.at("state_version");
	s.high_water = j.at("high_water");
	s.cached_at = j.at("cached_at");
	s.flush = j.at("flush");
	s.topics = j.at("topics").get<std::vector<std::string>>();
	s.predecessor_refresh_required = j.at("predecessor_refresh_required");
	if (!j.at("last_completed_sync").is_null())
		s.last_completed_sync = local_sync_from(j.at("last_completed_sync"), s);
	if (j.contains("initial_request") && !j.at("initial_request").is_null()) {
		s.initial_request = local_create_from(j.at("initial_request"));
		if (!s.session.empty() || !s.initial_request->predecessor_session_id.empty())
			throw std::runtime_error("initial_create_invariant");
	}
	if (!j.at("successor_request").is_null())
		s.successor_request = local_create_from(j.at("successor_request"));
	if (!j.at("staged_successor").is_null())
		s.staged_successor = local_session_from(j.at("staged_successor"));
	s.history = j.at("history").get<std::vector<std::size_t>>();
	s.cursor = j.at("cursor");
	s.next_item = j.at("next_item");
	const int a = j.at("availability"), d = j.at("delivery");
	if (a < 0 || a > 6 || d < 0 || d > 4)
		throw std::runtime_error("enum");
	s.availability = static_cast<StudyAvailability>(a);
	s.delivery = static_cast<StudyDelivery>(d);
	s.tracking_paused = j.at("tracking_paused");
	s.page_complete = j.at("page_complete");
	s.rejected_receipts =
	    j.at("rejected_receipts").get<std::vector<std::pair<std::string, std::string>>>();
	std::set<std::string> ids;
	for (const auto &v : j.at("items")) {
		Item i;
		i.card = card_from(v.at("card"));
		validate_card(i.card);
		if (!ids.insert(i.card.session_item_id).second)
			throw std::runtime_error("duplicate_item");
		int face = v.at("face");
		if (face < 0 || face > 1)
			throw std::runtime_error("face");
		i.face = static_cast<CardFace>(face);
		i.presented = v.at("presented");
		i.revealed = v.at("revealed");
		i.rejected = v.at("rejected");
		i.effective_rating = v.at("effective_rating");
		if (i.revealed && !i.presented)
			throw std::runtime_error("revealed_without_presented");
		s.items.push_back(std::move(i));
	}
	std::uint64_t last = 0;
	for (const auto &e : j.at("pending")) {
		auto event = event_from(e);
		if (event.client_sequence <= last || event.client_sequence >= s.next_sequence ||
		    event.session_id != s.session || ids.count(event.session_item_id) != 1)
			throw std::runtime_error("pending_invariant");
		last = event.client_sequence;
		s.pending.push_back(std::move(event));
	}
	if (!j.at("undo").is_null()) {
		const auto &u = j.at("undo");
		s.undo = Undo{u.at("event_id"), u.at("item_id"), u.at("group_id")};
		if (!ids.count(s.undo->item_id))
			throw std::runtime_error("undo_item");
	}
	if (!j.at("in_flight").is_null()) {
		const auto &f = j.at("in_flight");
		StudySyncRequest r;
		r.sync_id = f.at("sync_id");
		r.session_id = s.session;
		r.client_stream_id = s.stream;
		r.after_position = f.at("after_position");
		r.expected_state_version = f.at("expected_state_version");
		r.limit = f.at("limit");
		for (const auto &e : f.at("events"))
			r.events.push_back(event_from(e));
		s.in_flight = std::move(r);
	}
	if (s.items.size() > bounds.maximum_cards ||
	    s.pending.size() + (s.undo ? 1 : 0) > bounds.maximum_pending_events ||
	    s.history.size() > bounds.maximum_history ||
	    s.rejected_receipts.size() > bounds.maximum_pending_events ||
	    s.next_item > s.items.size() || (!s.history.empty() && s.cursor >= s.history.size()) ||
	    s.next_sequence == 0 || s.next_sequence > 9223372036854775807ULL ||
	    s.generation > 9007199254740991ULL || s.stream.empty())
		throw std::runtime_error("bounds");
	for (auto index : s.history)
		if (index >= s.items.size())
			throw std::runtime_error("history");
	if (s.availability == StudyAvailability::ready && s.history.empty())
		throw std::runtime_error("ready_without_card");
	if (s.undo) {
		const auto item = std::find_if(s.items.begin(), s.items.end(), [&](const auto &i) {
			return i.card.session_item_id == s.undo->item_id;
		});
		if (item == s.items.end() || item->effective_rating != s.undo->event_id)
			throw std::runtime_error("undo_target");
	}
	if (s.staged_successor) {
		if (s.staged_successor->cards.size() > bounds.maximum_cards ||
		    s.staged_successor->predecessor_session_id != s.session)
			throw std::runtime_error("staged_successor");
		for (const auto &card : s.staged_successor->cards)
			validate_card(card);
	}
	if (s.predecessor_refresh_required && (!s.staged_successor || !s.last_completed_sync))
		throw std::runtime_error("predecessor_refresh");

	// Provisional evidence is deliberately absent from disk. Do not redisplay its frame.
	if (auto *i = active(s)) {
		if (!i->rejected && (!i->presented || (i->face == CardFace::back && !i->revealed)))
			s.availability = StudyAvailability::resume_required;
	}
	return s;
}
} // namespace
const char *study_availability_name(StudyAvailability a)
{
	switch (a) {
	case StudyAvailability::loading:
		return "loading";
	case StudyAvailability::ready:
		return "ready";
	case StudyAvailability::empty:
		return "empty";
	case StudyAvailability::refill_required:
		return "refill_required";
	case StudyAvailability::resume_required:
		return "resume_required";
	case StudyAvailability::error:
		return "error";
	}
	return "invalid";
}
const char *study_delivery_name(StudyDelivery d)
{
	switch (d) {
	case StudyDelivery::none:
		return "none";
	case StudyDelivery::queued_locally:
		return "queued_locally";
	case StudyDelivery::syncing:
		return "syncing";
	case StudyDelivery::saved:
		return "saved";
	case StudyDelivery::rejected:
		return "rejected";
	}
	return "invalid";
}
struct FlashCardSession::Implementation {
	std::shared_ptr<StudyStore> store;
	StudyBounds bounds;
	State state;
	FlashCardSnapshot view;
	Implementation(std::shared_ptr<StudyStore> s, StudyBounds b)
	    : store(std::move(s)), bounds(b)
	{
	}
	void project()
	{
		view.availability = state.availability;
		view.delivery = state.delivery;
		view.session_id = state.session;
		view.render_generation = state.generation;
		view.history_size = state.history.size();
		view.pending_event_count = state.pending.size();
		view.tracking_paused = state.tracking_paused;
		view.durable_storage = store->durable();
		view.can_undo = state.undo.has_value();
		view.flash_card.reset();
		view.provisional = false;
		view.can_flip = false;
		view.can_back = false;
		view.can_next = state.availability == StudyAvailability::refill_required;
		view.can_rate = false;
		if (const auto *i = active(state)) {
			view.flash_card = i->card;
			view.face = i->face;
			view.provisional = i->provisional_presented || i->provisional_revealed;
			view.can_flip = true;
			view.can_back = state.cursor > 0;
			view.can_next = true;
			view.can_rate = i->face == CardFace::back && i->effective_rating.empty() &&
					!i->rejected;
		}
	}
	Result fail(VocabularyPanelError e, bool pause = false)
	{
		view.error = e;
		view.tracking_paused = pause || state.tracking_paused;
		return Result::failure(std::move(e));
	}
	Result commit(State candidate)
	{
		if (candidate.generation >= 9007199254740991ULL)
			return fail(
			    error("STUDY.GENERATION_EXHAUSTED", "Render generation exhausted"));
		++candidate.generation;
		std::string payload;
		try {
			payload = encode(candidate);
		} catch (const std::exception &) {
			return fail(error("STUDY.STATE_INVALID", "State cannot be serialized"));
		}
		if (payload.size() > bounds.maximum_state_bytes)
			return fail(error("STUDY.STORAGE_FULL",
					  "Serialized study state exceeds configured byte ceiling"),
				    true);
		const auto result = store->commit(payload);
		if (!result.has_value())
			return fail(result.error(), true);
		state = std::move(candidate);
		view.error.reset();
		project();
		return Result::success(view);
	}
	bool space(const State &s, std::size_t amount) const
	{
		return s.pending.size() + (s.undo ? 1 : 0) + amount <=
		       bounds.maximum_pending_events;
	}
	void append(State &s, Item &i, const std::string &kind, const std::string &group,
		    const std::string &rating = {}, const std::string &target = {})
	{
		s.pending.push_back({new_study_uuid(), s.next_sequence++, s.session,
				     i.card.session_item_id, i.card.content_revision, kind, rating,
				     target, group, s.profile, i.card.vocabulary_card_id,
				     i.card.content_sha256});
		s.flush.clear();

		s.delivery = StudyDelivery::queued_locally;
		if (kind == "presented") {
			i.presented = true;
			i.provisional_presented = false;
		}
		if (kind == "revealed") {
			i.revealed = true;
			i.provisional_revealed = false;
		}
	}
	bool promote(State &s, Item &i, const std::string &group, bool reveal = false)
	{
		if (i.rejected)
			return !i.provisional_presented && !i.provisional_revealed && !reveal;
		const bool p = !i.presented, r = !i.revealed && (i.provisional_revealed || reveal);
		if (!space(s, static_cast<std::size_t>(p) + static_cast<std::size_t>(r)))
			return false;
		if (p)
			append(s, i, "presented", group);
		if (r)
			append(s, i, "revealed", group);
		return true;
	}
	bool initialize(State &s, const StudySession &session,
			StudyInputSource source = StudyInputSource::touch)
	{
		auto stream = s.stream;
		auto sequence = s.next_sequence;
		auto generation = s.generation;
		State fresh;
		fresh.stream = stream;
		fresh.next_sequence = sequence;
		fresh.generation = generation;
		fresh.session = session.session_id;
		fresh.profile = session.profile_ref;
		fresh.scheduler = session.scheduler_json;
		fresh.generated_at = session.generated_at;
		fresh.state_version = session.state_version;
		fresh.high_water = std::max(s.high_water, session.profile_ingestion_high_water);
		fresh.page_complete = session.page_complete;
		fresh.cached_at = now_seconds();
		fresh.topics = session.topic_refs.empty() ? s.topics : session.topic_refs;
		std::set<std::string> ids;
		for (const auto &card : session.cards) {
			validate_card(card);
			if (!ids.insert(card.session_item_id).second)
				throw std::runtime_error("duplicate");
			Item item;
			item.card = card;
			fresh.items.push_back(std::move(item));
		}
		if (fresh.items.empty())
			fresh.availability = StudyAvailability::empty;
		else if (!advance(fresh, source, new_study_uuid()))
			return false;
		s = std::move(fresh);
		return true;
	}
	bool evict_acknowledged_card(State &s)
	{
		for (std::size_t index = 0; index < s.next_item; ++index) {
			const auto &item = s.items[index];
			if (item.rejected ||
			    std::find(s.history.begin(), s.history.end(), index) !=
				s.history.end() ||
			    (s.undo && s.undo->item_id == item.card.session_item_id) ||
			    std::any_of(s.pending.begin(), s.pending.end(), [&](const auto &e) {
				    return e.session_item_id == item.card.session_item_id;
			    }))
				continue;
			s.items.erase(s.items.begin() + static_cast<std::ptrdiff_t>(index));
			for (auto &history : s.history)
				if (history > index)
					--history;
			--s.next_item;
			return true;
		}
		return false;
	}
	bool advance(State &s, StudyInputSource source, const std::string &group,
		     bool rating = false)
	{
		if (!s.history.empty() && s.cursor + 1 < s.history.size()) {
			++s.cursor;
			s.availability = StudyAvailability::ready;
			return true;
		}
		if (s.next_item >= s.items.size()) {
			s.availability = StudyAvailability::refill_required;
			return true;
		}
		auto &i = s.items.at(s.next_item);
		if (!i.presented) {
			if (source == StudyInputSource::voice)
				i.provisional_presented = true;
			else if (space(s, 1))
				append(s, i, "presented", group);
			else {
				if (rating) {
					s.availability = StudyAvailability::refill_required;
					s.tracking_paused = true;
					return true;
				}
				return false;
			}
		}
		s.history.push_back(s.next_item++);
		if (s.history.size() > bounds.maximum_history)
			s.history.erase(s.history.begin());
		s.cursor = s.history.size() - 1;
		s.availability = StudyAvailability::ready;
		return true;
	}
};
FlashCardSession::FlashCardSession(std::shared_ptr<StudyStore> store, StudyBounds b)
    : implementation_(std::make_unique<Implementation>(std::move(store), b))
{
	if (!implementation_->store || b.maximum_cards == 0 || b.maximum_cards > 4096 ||
	    b.maximum_history == 0 || b.maximum_history > b.maximum_cards ||
	    b.maximum_pending_events < 2 || b.maximum_pending_events > 64 ||
	    b.maximum_state_bytes == 0 || b.maximum_state_bytes > 64 * 1024 * 1024 ||
	    b.prefetch_low_water > b.maximum_cards || b.maximum_offline_seconds == 0)
		throw std::invalid_argument("STUDY.BOUNDS_INVALID");
	implementation_->project();
}
FlashCardSession::~FlashCardSession() = default;
const FlashCardSnapshot &FlashCardSession::snapshot() const { return implementation_->view; }
StudyIntentBinding FlashCardSession::current_binding() const
{
	const auto &s = snapshot();
	return {s.session_id, s.flash_card ? s.flash_card->session_item_id : "",
		s.flash_card ? s.flash_card->content_revision : "", s.render_generation};
}
Result FlashCardSession::restore()
{
	auto &p = *implementation_;
	auto result = p.store->read();
	if (!result.has_value())
		return p.fail(result.error());
	if (!result.value())
		return Result::success(p.view);
	try {
		auto s = decode(*result.value(), p.bounds);
		p.view.stale = s.cached_at == 0 ||
			       now_seconds() > s.cached_at + p.bounds.maximum_offline_seconds;
		p.view.connectivity = StudyConnectivity::offline;
		return p.commit(std::move(s));
	} catch (const std::exception &) {
		p.view.availability = StudyAvailability::error;
		return p.fail(
		    error("STUDY.CACHE_CORRUPT", "Cached study state is corrupt or incompatible"));
	}
}
VocabularyPanelResult<StudyCreateRequest>
FlashCardSession::prepare_create(const std::string &profile_ref,
				 const std::vector<std::string> &topic_refs)
{
	using R = VocabularyPanelResult<StudyCreateRequest>;
	auto &p = *implementation_;
	if (!p.state.session.empty())
		return R::failure(error("STUDY.SESSION_ALREADY_INSTALLED",
					"Initial creation cannot replace an installed session"));
	if (p.state.initial_request) {
		const auto &request = *p.state.initial_request;
		if (request.profile_ref != profile_ref || request.topic_refs != topic_refs)
			return R::failure(error(
			    "STUDY.CREATE_CONFIG_CHANGED",
			    "Configured scope differs from the durable initial creation request"));
		return R::success(request);
	}
	StudyCreateRequest request;
	request.creation_request_id = new_study_uuid();
	request.profile_ref = profile_ref;
	request.topic_refs = topic_refs;
	request.limit = std::min<std::size_t>(64, p.bounds.maximum_cards);
	const auto encoded = encode_study_create(request);
	if (!encoded.has_value())
		return R::failure(encoded.error());
	State candidate = p.state;
	candidate.initial_request = request;
	candidate.profile = profile_ref;
	candidate.topics = topic_refs;
	auto committed = p.commit(std::move(candidate));
	if (!committed.has_value())
		return R::failure(committed.error());
	return R::success(std::move(request));
}
Result FlashCardSession::install_session(std::vector<FlashCard> cards, std::string id)
{
	StudySession session;
	session.session_id = std::move(id);
	session.profile_ref = "fixture-default";
	session.lifecycle = cards.empty() ? "waiting_for_corpus" : "active";
	session.state_version = 1;
	session.cards = std::move(cards);
	return install_session(std::move(session));
}
Result FlashCardSession::install_session(StudySession session)
{
	auto &p = *implementation_;
	if (p.state.initial_request &&
	    (session.profile_ref != p.state.initial_request->profile_ref ||
	     session.topic_refs != p.state.initial_request->topic_refs))
		return p.fail(error("STUDY.CREATE_RESPONSE_MISMATCH",
				    "Session response does not match the durable initial scope"));
	if (!p.state.pending.empty())
		return p.fail(
		    error("STUDY.PENDING_EVENTS_RETAINED",
			  "Pending events must be acknowledged before replacing a session"));
	if (session.session_id.empty() || session.cards.size() > p.bounds.maximum_cards)
		return p.fail(error("LEXINET.RESPONSE_SCHEMA_INVALID",
				    "Invalid session identity or card bound"));
	if (session.session_id == p.state.session && !p.state.session.empty())
		return p.fail(error(
		    "STUDY.SESSION_ALREADY_INSTALLED",
		    "Resume existing session through sync; replacing it would discard evidence"));
	State s = p.state;
	if (!s.session.empty() && session.predecessor_session_id == s.session) {
		if (!s.successor_request)
			return p.fail(error("STUDY.SUCCESSOR_UNREQUESTED",
					    "Successor must match the durable barrier request"));
		if (s.undo || !s.pending.empty() || s.in_flight) {
			s.staged_successor = std::move(session);
			s.predecessor_refresh_required = true;
			return p.commit(std::move(s));
		}
	}
	try {
		if (!p.initialize(s, session))
			return p.fail(error("STUDY.TRACKING_PAUSED",
					    "New session cannot durably present first card"),
				      true);
	} catch (const std::exception &) {
		return p.fail(
		    error("LEXINET.RESPONSE_SCHEMA_INVALID", "Invalid immutable card projection"));
	}
	return p.commit(std::move(s));
}
Result FlashCardSession::apply_intent(StudyIntent intent, StudyInputSource source,
				      const StudyIntentBinding &binding)
{
	auto &p = *implementation_;
	if (p.state.next_sequence > 9223372036854775803ULL)
		return p.fail(
		    error("STUDY.SEQUENCE_EXHAUSTED", "Durable stream sequence exhausted"));
	const auto current = current_binding();
	if (binding.session_id != current.session_id ||
	    binding.session_item_id != current.session_item_id ||
	    binding.content_revision != current.content_revision ||
	    binding.render_generation != current.render_generation)
		return p.fail(error("STUDY.STALE_INTENT",
				    "Study intent does not match the exact active frame"));
	if (intent == StudyIntent::rating_undo && source != StudyInputSource::touch)
		return p.fail(error("STUDY.UNDO_TOUCH_REQUIRED", "Rating Undo requires touch"));
	State s = p.state;
	const std::string group = new_study_uuid();
	if (intent == StudyIntent::rating_undo) {
		if (!s.undo)
			return p.fail(
			    error("STUDY.UNDO_UNAVAILABLE", "No current durable rating lease"));
		auto lease = *s.undo;
		auto it = std::find_if(s.items.begin(), s.items.end(), [&](const auto &i) {
			return i.card.session_item_id == lease.item_id;
		});
		if (it == s.items.end() || it->effective_rating != lease.event_id)
			return p.fail(error("STUDY.UNDO_INVARIANT",
					    "Durable rating lease target is inconsistent"));
		s.undo.reset();
		p.append(s, *it, "rating_voided", group, "", lease.event_id);
		it->effective_rating.clear();
		return p.commit(std::move(s));
	}
	// Only an accepted intent expires the lease: candidate state is isolated until commit.
	s.undo.reset();
	s.tracking_paused = false;
	if (intent == StudyIntent::resume && s.availability == StudyAvailability::resume_required) {
		if (s.history.empty())
			return p.fail(error("STUDY.RESUME_INVALID", "No resumable item"));
		s.availability = StudyAvailability::ready;
		auto *i = active(s);
		if (source == StudyInputSource::touch) {
			if (!p.promote(s, *i, group, i->face == CardFace::back))
				return p.fail(error("STUDY.TRACKING_PAUSED",
						    "No capacity for resume evidence"),
					      true);
		} else {
			i->provisional_presented = !i->presented;
			i->provisional_revealed = i->face == CardFace::back && !i->revealed;
		}
		return p.commit(std::move(s));
	}
	if (s.availability == StudyAvailability::refill_required && intent == StudyIntent::next) {
		if (s.staged_successor) {
			if (!s.pending.empty() || s.in_flight)
				return p.fail(error("STUDY.FLUSH_BARRIER_REQUIRED",
						    "A late correction must flush before "
						    "activating the staged successor"));
			const auto successor = *s.staged_successor;
			try {
				p.initialize(s, successor, source);
			} catch (const std::exception &) {
				return p.fail(
				    error("STUDY.CACHE_CORRUPT", "Staged successor is invalid"));
			}
			return p.commit(std::move(s));
		}

		if (!p.advance(s, source, group))
			return p.fail(
			    error("STUDY.TRACKING_PAUSED", "No capacity for first presentation"),
			    true);
		return p.commit(std::move(s));
	}
	auto *i = active(s);
	if (!i)
		return p.fail(error("STUDY.INTENT_UNAVAILABLE", "No active study card"));
	const bool rating = intent == StudyIntent::rate_again || intent == StudyIntent::rate_hard ||
			    intent == StudyIntent::rate_good || intent == StudyIntent::rate_easy;
	if (rating && (i->face != CardFace::back || !i->effective_rating.empty() || i->rejected))
		return p.fail(error("STUDY.RATING_UNAVAILABLE",
				    "Rating requires an unrated, revealed, unrejected item"));
	if (source == StudyInputSource::touch || rating) {
		if (!p.promote(s, *i, group))
			return p.fail(error("STUDY.TRACKING_PAUSED",
					    "No capacity for current-item prerequisite evidence"),
				      true);
	}
	switch (intent) {
	case StudyIntent::flip:
		if (i->face == CardFace::front && !i->revealed) {
			if (source == StudyInputSource::voice) {
				i->provisional_revealed = true;
			} else if (!p.promote(s, *i, group, true))
				return p.fail(
				    error("STUDY.TRACKING_PAUSED", "No capacity for first reveal"),
				    true);
		}
		i->face = i->face == CardFace::front ? CardFace::back : CardFace::front;
		break;
	case StudyIntent::next:
		if (!p.advance(s, source, group))
			return p.fail(
			    error("STUDY.TRACKING_PAUSED", "No capacity for first presentation"),
			    true);
		break;
	case StudyIntent::back:
		if (s.cursor == 0)
			return p.fail(error("STUDY.HISTORY_BOUNDARY", "No earlier retained card"));
		--s.cursor;
		break;
	case StudyIntent::rate_again:
	case StudyIntent::rate_hard:
	case StudyIntent::rate_good:
	case StudyIntent::rate_easy: {
		if (!p.space(s, 2))
			return p.fail(error("STUDY.TRACKING_PAUSED",
					    "Rating and Undo reservation exceed the outbox bound"),
				      true);
		const std::string value = intent == StudyIntent::rate_again  ? "again"
					  : intent == StudyIntent::rate_hard ? "hard"
					  : intent == StudyIntent::rate_good ? "good"
									     : "easy";
		p.append(s, *i, "rated", group, value);
		i->effective_rating = s.pending.back().event_id;
		s.undo = Undo{i->effective_rating, i->card.session_item_id, group};
		p.advance(s, source, group, true);
		break;
	}
	default:
		return p.fail(error("STUDY.INTENT_UNAVAILABLE",
				    "Intent is not valid in the current study state"));
	}
	return p.commit(std::move(s));
}
VocabularyPanelResult<StudySyncRequest> FlashCardSession::prepare_sync()
{
	auto &p = *implementation_;
	using Sync = VocabularyPanelResult<StudySyncRequest>;
	if (p.state.session.empty())
		return Sync::failure(error("STUDY.SESSION_REQUIRED", "No session to synchronize"));
	if (p.state.in_flight)
		return Sync::success(*p.state.in_flight);
	State s = p.state;
	if (s.predecessor_refresh_required) {
		if (!s.last_completed_sync)
			return Sync::failure(error(
			    "STUDY.FLUSH_BARRIER_REQUIRED",
			    "Completed predecessor refresh requires retained acknowledged sync"));
		s.in_flight = *s.last_completed_sync;
		auto result = p.commit(std::move(s));
		if (!result.has_value())
			return Sync::failure(result.error());
		return Sync::success(*p.state.in_flight);
	}
	StudySyncRequest request;
	request.sync_id = new_study_uuid();
	request.session_id = s.session;
	request.client_stream_id = s.stream;
	request.events = s.pending;
	request.expected_state_version = s.state_version;
	request.after_position = s.items.empty() ? -1 : s.items.back().card.position;
	std::size_t reclaimable = 0;
	for (std::size_t index = 0; index < s.next_item; ++index) {
		const auto &item = s.items[index];
		if (!item.rejected &&
		    std::find(s.history.begin(), s.history.end(), index) == s.history.end() &&
		    (!s.undo || s.undo->item_id != item.card.session_item_id))
			++reclaimable;
	}
	const auto capacity = s.items.size() - s.next_item <= p.bounds.prefetch_low_water
				  ? p.bounds.maximum_cards - s.items.size() + reclaimable
				  : 0;
	request.limit = std::max<std::size_t>(1, std::min<std::size_t>(64, capacity));
	if (capacity == 0 && !s.items.empty())
		request.after_position = s.items.back().card.position - 1;
	s.in_flight = request;
	if (!s.pending.empty())
		s.delivery = StudyDelivery::syncing;
	const auto committed = p.commit(std::move(s));
	if (!committed.has_value())
		return Sync::failure(committed.error());
	return Sync::success(request);
}
Result FlashCardSession::accept_sync(const StudySyncResponse &response)
{
	auto &p = *implementation_;
	State s = p.state;
	if (!s.in_flight || response.sync_id != s.in_flight->sync_id ||
	    response.client_stream_id != s.stream || response.session.session_id != s.session ||
	    response.session.state_version < s.state_version ||
	    response.profile_ingestion_high_water < s.high_water ||
	    response.receipts.size() != s.in_flight->events.size())
		return p.fail(error("STUDY.SYNC_RESPONSE_MISMATCH",
				    "Synchronization response does not match the durable request"));
	std::set<std::string> rejected_groups;
	std::string rejection;
	for (std::size_t n = 0; n < response.receipts.size(); ++n) {
		const auto &r = response.receipts[n];
		const auto &e = s.in_flight->events[n];
		if (r.event_id != e.event_id || r.client_sequence != e.client_sequence ||
		    (r.disposition != "accepted" && r.disposition != "semantic_duplicate" &&
		     r.disposition != "terminal_rejected"))
			return p.fail(error("STUDY.RECEIPT_MISMATCH",
					    "Terminal receipt identity or disposition invalid"));
		if (r.disposition == "terminal_rejected") {
			rejected_groups.insert(e.group_id);
			rejection = r.code;
		}
	}
	if (!response.receipts.empty() &&
	    response.adjudicated_through_sequence != response.receipts.back().client_sequence)
		return p.fail(error("STUDY.RECEIPT_SEQUENCE_INVALID",
				    "Adjudicated-through sequence mismatches receipts"));
	std::set<std::string> acknowledged;
	for (const auto &r : response.receipts)
		acknowledged.insert(r.event_id);
	for (const auto &e : s.in_flight->events)
		if (rejected_groups.count(e.group_id)) {
			for (auto &i : s.items)
				if (i.card.session_item_id == e.session_item_id) {
					i.rejected = true;
					if (e.event_kind == "presented")
						i.presented = false;
					if (e.event_kind == "revealed")
						i.revealed = false;
					if (e.event_kind == "rated")
						i.effective_rating.clear();
				}
			s.rejected_receipts.emplace_back(e.event_id, rejection);
			if (s.rejected_receipts.size() > p.bounds.maximum_pending_events)
				s.rejected_receipts.erase(s.rejected_receipts.begin());
			if (s.undo && s.undo->group_id == e.group_id)
				s.undo.reset();
		}
	s.pending.erase(
	    std::remove_if(s.pending.begin(), s.pending.end(),
			   [&](const auto &e) { return acknowledged.count(e.event_id) != 0; }),
	    s.pending.end());
	bool refill_deferred = false;
	// Refill retains current/successor render and Undo; immutable identities may not change.
	for (const auto &card : response.session.cards) {
		auto it = std::find_if(s.items.begin(), s.items.end(), [&](const auto &i) {
			return i.card.session_item_id == card.session_item_id;
		});
		if (it != s.items.end()) {
			if (card_json(it->card) != card_json(card))
				return p.fail(error("LEXINET.IMMUTABLE_CARD_CHANGED",
						    "Issued card identity has changed content"));
		} else {
			if (!s.items.empty() && card.position != s.items.back().card.position + 1)
				return p.fail(error(
				    "LEXINET.PAGE_POSITION_INVALID",
				    "Refill cannot reissue an evicted item or skip a position"));
			if (s.items.size() >= p.bounds.maximum_cards &&
			    !p.evict_acknowledged_card(s)) {
				refill_deferred = true;
				break;
			}
			try {
				validate_card(card);
			} catch (const std::exception &) {
				return p.fail(error("LEXINET.RESPONSE_SCHEMA_INVALID",
						    "Refill card is invalid"));
			}
			Item item;
			item.card = card;
			s.items.push_back(std::move(item));
		}
	}
	s.last_completed_sync = s.in_flight;
	s.predecessor_refresh_required = false;
	s.in_flight.reset();
	s.state_version = response.session.state_version;
	s.high_water = response.profile_ingestion_high_water;
	s.page_complete = response.session.page_complete && !refill_deferred;
	s.cached_at = now_seconds();
	s.flush = s.pending.empty() ? response.flush_receipt_json : "";
	s.delivery = !s.rejected_receipts.empty() ? StudyDelivery::rejected
		     : s.pending.empty()	  ? StudyDelivery::saved
						  : StudyDelivery::queued_locally;
	auto result = p.commit(std::move(s));
	if (result.has_value()) {
		p.view.connectivity = StudyConnectivity::online;
		p.view.stale = false;
		if (refill_deferred) {
			p.view.tracking_paused = true;
			p.view.error =
			    error("STUDY.CACHE_FULL", "Acknowledgements saved; refill deferred "
						      "until referenced cards can be evicted");
		}
		if (!rejection.empty())
			p.view.error = error("STUDY.TRACKING_REJECTED",
					     "Lexinet terminally rejected study evidence");
		return Result::success(p.view);
	}
	return result;
}
VocabularyPanelResult<std::string> FlashCardSession::predecessor_flush_receipt() const
{
	const auto &s = implementation_->state;
	if (!s.pending.empty() || s.in_flight || s.flush.empty())
		return VocabularyPanelResult<std::string>::failure(error(
		    "STUDY.FLUSH_BARRIER_REQUIRED",
		    "Every predecessor event must be acknowledged before successor creation"));
	return VocabularyPanelResult<std::string>::success(s.flush);
}
VocabularyPanelResult<StudyCreateRequest> FlashCardSession::prepare_successor()
{
	auto &p = *implementation_;
	using R = VocabularyPanelResult<StudyCreateRequest>;
	if (p.state.successor_request)
		return R::success(*p.state.successor_request);
	auto barrier = predecessor_flush_receipt();
	if (!barrier.has_value())
		return R::failure(barrier.error());
	if (!p.state.page_complete || p.state.availability != StudyAvailability::refill_required ||
	    !p.state.rejected_receipts.empty())
		return R::failure(error(
		    "STUDY.SUCCESSOR_UNAVAILABLE",
		    "Successor requires a complete page and an unrejected exhausted local view"));
	State s = p.state;
	StudyCreateRequest request;
	request.creation_request_id = new_study_uuid();
	request.profile_ref = s.profile;
	request.topic_refs = s.topics;
	request.predecessor_session_id = s.session;
	request.predecessor_flush_receipt_json = s.flush;
	request.limit = std::min<std::size_t>(64, p.bounds.maximum_cards);
	s.successor_request = request;
	auto result = p.commit(std::move(s));
	if (!result.has_value())
		return R::failure(result.error());
	return R::success(std::move(request));
}
Result FlashCardSession::leave_surface()
{
	auto &p = *implementation_;
	State s = p.state;
	s.undo.reset();
	for (auto &i : s.items) {
		i.provisional_presented = false;
		i.provisional_revealed = false;
	}
	if (auto *i = active(s))
		if (!i->presented || (i->face == CardFace::back && !i->revealed))
			s.availability = StudyAvailability::resume_required;
	return p.commit(std::move(s));
}
void FlashCardSession::set_connectivity(StudyConnectivity c, bool stale)
{
	implementation_->view.connectivity = c;
	implementation_->view.stale = stale;
}
void FlashCardSession::set_clock_trusted(bool trusted)
{
	implementation_->view.clock_trusted = trusted;
}
void FlashCardSession::set_boundary_error(VocabularyPanelError e)
{
	implementation_->view.error = std::move(e);
	implementation_->view.connectivity = StudyConnectivity::offline;
	if (!implementation_->view.flash_card)
		implementation_->view.availability = StudyAvailability::error;
}
bool FlashCardSession::needs_sync() const
{
	const auto &p = *implementation_;
	const auto &s = p.state;
	return !s.pending.empty() || s.in_flight.has_value() || s.predecessor_refresh_required ||
	       (!s.page_complete && s.items.size() - s.next_item <= p.bounds.prefetch_low_water);
}
const std::vector<StudyEvent> &FlashCardSession::pending_events() const
{
	return implementation_->state.pending;
}
const std::string &FlashCardSession::client_stream_id() const
{
	return implementation_->state.stream;
}
} // namespace vocabulary_panel
