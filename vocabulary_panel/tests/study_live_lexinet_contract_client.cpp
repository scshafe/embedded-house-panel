// Live cross-repository study.v1 contract client. The orchestrator
// (scripts/lexinet/live_contract.py) owns the disposable Lexinet, PostgreSQL,
// seeded corpus and bearer; this program drives the production client and the
// durable reducer and prints exactly one JSON report line per scenario.
// Exit status: 0 when every check passed, 1 when a check failed, 2 on usage
// errors, 86 after a deliberate crash_after_send exit.
#include "bhp/structured_logger.h"
#include "vocabulary_panel/flash_card_session.h"
#include "vocabulary_panel/http_lexinet_client.h"
#include "vocabulary_panel/study_store.h"
#include <nlohmann/json.hpp>
#include <openssl/sha.h>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

using namespace vocabulary_panel;
using Json = nlohmann::ordered_json;

namespace
{
constexpr int kCrashExit = 86;

struct Options {
	std::string scenario, base_url, credential_file, state_directory, profile_ref;
	std::string expect_session, expect_sync_id;
	std::vector<std::string> topic_refs;
};

[[noreturn]] void usage(const std::string &reason)
{
	std::cout << Json{{"ok", false}, {"usage_error", reason}}.dump() << '\n';
	std::exit(2);
}

Options parse(int argc, char **argv)
{
	Options o;
	for (int i = 1; i < argc; ++i) {
		const std::string key = argv[i];
		if (i + 1 >= argc)
			usage("missing value for " + key);
		const std::string value = argv[++i];
		if (key == "--scenario")
			o.scenario = value;
		else if (key == "--base-url")
			o.base_url = value;
		else if (key == "--credential-file")
			o.credential_file = value;
		else if (key == "--state-directory")
			o.state_directory = value;
		else if (key == "--profile-ref")
			o.profile_ref = value;
		else if (key == "--topic-ref")
			o.topic_refs.push_back(value);
		else if (key == "--expect-session")
			o.expect_session = value;
		else if (key == "--expect-sync-id")
			o.expect_sync_id = value;
		else
			usage("unknown option " + key);
	}
	if (o.scenario.empty() || o.base_url.empty() || o.credential_file.empty() ||
	    o.state_directory.empty() || o.profile_ref.empty() || o.topic_refs.empty())
		usage("--scenario, --base-url, --credential-file, --state-directory, "
		      "--profile-ref and --topic-ref are required");
	return o;
}

std::string sha256_hex(const std::string &value)
{
	unsigned char digest[SHA256_DIGEST_LENGTH];
	SHA256(reinterpret_cast<const unsigned char *>(value.data()), value.size(), digest);
	std::ostringstream out;
	for (unsigned char byte : digest)
		out << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
	return out.str();
}

/** Last intent failure, reported by the scenario that observed it. */
std::optional<VocabularyPanelError> last_intent_error;

/** Accumulates named checks and facts; prints one machine-readable line. */
class Report
{
      public:
	explicit Report(std::string scenario)
	{
		document_["scenario"] = std::move(scenario);
		document_["evidence"] = "simulation_not_hardware_evidence";
		document_["checks"] = Json::object();
		document_["facts"] = Json::object();
	}
	bool check(const std::string &name, bool passed)
	{
		document_["checks"][name] = passed;
		ok_ = ok_ && passed;
		return passed;
	}
	template <typename Value> void fact(const std::string &name, Value value)
	{
		document_["facts"][name] = std::move(value);
	}
	void error(const std::string &name, const VocabularyPanelError &e)
	{
		document_["facts"][name] = {{"code", e.error_code}, {"retryable", e.retryable}};
	}
	int finish()
	{
		if (last_intent_error)
			document_["facts"]["last_intent_error"] = last_intent_error->error_code;
		document_["ok"] = ok_;
		std::cout << document_.dump() << '\n';
		std::cout.flush();
		return ok_ ? 0 : 1;
	}
	bool ok() const { return ok_; }

      private:
	Json document_;
	bool ok_{true};
};

HttpLexinetConfig client_config(const Options &o)
{
	HttpLexinetConfig c;
	c.lexinet_base_url = o.base_url;
	c.credential_file_path = o.credential_file;
	c.transport_budget = {1000, 5000, 1048576};
	c.allow_insecure_http_for_tests = true;
	c.maximum_snapshot_age_seconds = 3600;
	return c;
}

bool act(FlashCardSession &s, StudyIntent intent)
{
	auto applied = s.apply_intent(intent, StudyInputSource::touch, s.current_binding());
	if (!applied.has_value())
		last_intent_error = applied.error();
	return applied.has_value();
}

Json view_of(const FlashCardSession &s)
{
	const auto &v = s.snapshot();
	return {{"availability", study_availability_name(v.availability)},
		{"delivery", study_delivery_name(v.delivery)},
		{"face", v.face == CardFace::front ? "front" : "back"},
		{"history_size", v.history_size},
		{"pending_event_count", v.pending_event_count},
		{"can_flip", v.can_flip},
		{"can_next", v.can_next},
		{"can_rate", v.can_rate},
		{"can_undo", v.can_undo},
		{"has_card", v.flash_card.has_value()}};
}

/** Deliver the whole outbox; each request is replayed once to prove idempotence. */
bool deliver(FlashCardSession &s, HttpLexinetClient &client, Report &r, const std::string &label,
	     const std::string &expected_first_sync_id = {})
{
	for (int round = 0; round < 6 && s.needs_sync(); ++round) {
		auto request = s.prepare_sync();
		if (!r.check(label + ".prepare_sync_" + std::to_string(round), request.has_value()))
			return false;
		if (round == 0 && !expected_first_sync_id.empty())
			r.check(label + ".durable_sync_id_reused",
				request.value().sync_id == expected_first_sync_id);
		auto response = client.sync(request.value(), label + "-sync");
		if (!response.has_value()) {
			r.error(label + ".sync_error", response.error());
			r.check(label + ".sync_" + std::to_string(round), false);
			return false;
		}
		auto replay = client.sync(request.value(), label + "-replay");
		r.check(label + ".replay_identical_flush_receipt_" + std::to_string(round),
			replay.has_value() &&
			    replay.value().flush_receipt_json == response.value().flush_receipt_json &&
			    replay.value().receipts.size() == response.value().receipts.size());
		auto accepted = s.accept_sync(response.value());
		if (!accepted.has_value())
			r.error(label + ".accept_error", accepted.error());
		if (!r.check(label + ".accept_" + std::to_string(round), accepted.has_value()))
			return false;
		r.fact(label + ".flush_receipt_sha256_" + std::to_string(round),
		       sha256_hex(response.value().flush_receipt_json));
	}
	return r.check(label + ".outbox_empty", s.pending_events().empty());
}

std::shared_ptr<FileStudyStore> open_store(const Options &o)
{
	return std::make_shared<FileStudyStore>(o.state_directory);
}

StudyCreateRequest fresh_resume_request(const Options &o)
{
	StudyCreateRequest request;
	request.creation_request_id = new_study_uuid();
	request.profile_ref = o.profile_ref;
	request.topic_refs = o.topic_refs;
	request.limit = 64;
	return request;
}

int create_resume(const Options &o, HttpLexinetClient &client)
{
	Report r("create_resume");
	FlashCardSession s(open_store(o));
	r.check("restore_empty_store", s.restore().has_value());
	auto request = s.prepare_create(o.profile_ref, o.topic_refs);
	if (!r.check("prepare_create", request.has_value()))
		return r.finish();
	auto created = client.create_or_resume(request.value(), "live-create");
	if (!created.has_value()) {
		r.error("create_error", created.error());
		r.check("create", false);
		return r.finish();
	}
	r.check("create", true);
	r.fact("session_id", created.value().session_id);
	r.fact("state_version", created.value().state_version);
	r.fact("card_count", created.value().cards.size());
	r.check("cards_issued", !created.value().cards.empty());
	auto replay = client.create_or_resume(request.value(), "live-create-replay");
	r.check("create_replay_same_session",
		replay.has_value() && replay.value().session_id == created.value().session_id);
	auto resumed = client.create_or_resume(fresh_resume_request(o), "live-create-resume");
	r.check("new_creation_id_resumes_active_scope",
		resumed.has_value() && resumed.value().session_id == created.value().session_id);
	auto conflicting = request.value();
	conflicting.limit = conflicting.limit == 64 ? 63 : 64;
	auto conflict = client.create_or_resume(conflicting, "live-create-conflict");
	r.check("create_idempotency_conflict",
		!conflict.has_value() && conflict.error().error_code == "IDEMPOTENCY_CONFLICT");
	auto installed = s.install_session(created.value());
	r.check("install_session", installed.has_value());
	r.check("ready_after_install", s.snapshot().availability == StudyAvailability::ready);
	return r.finish();
}

int study_and_sync(const Options &o, HttpLexinetClient &client)
{
	Report r("study_and_sync");
	FlashCardSession s(open_store(o));
	r.check("restore", s.restore().has_value());
	r.check("restored_session_matches",
		o.expect_session.empty() || s.snapshot().session_id == o.expect_session);
	r.check("durable_storage", s.snapshot().durable_storage);
	r.check("flip", act(s, StudyIntent::flip));
	r.check("rate_good", act(s, StudyIntent::rate_good));
	auto request = s.prepare_sync();
	if (!r.check("prepare_sync", request.has_value()))
		return r.finish();
	r.fact("event_count", request.value().events.size());
	auto response = client.sync(request.value(), "live-sync");
	if (!response.has_value()) {
		r.error("sync_error", response.error());
		r.check("sync", false);
		return r.finish();
	}
	std::size_t accepted = 0;
	for (const auto &receipt : response.value().receipts)
		accepted += receipt.disposition == "accepted";
	r.check("all_events_accepted", accepted == request.value().events.size());
	auto replay = client.sync(request.value(), "live-sync-replay");
	r.check("replay_identical_receipts_and_flush",
		replay.has_value() &&
		    replay.value().flush_receipt_json == response.value().flush_receipt_json &&
		    replay.value().receipts.size() == response.value().receipts.size());
	auto conflicting = request.value();
	conflicting.limit = conflicting.limit == 64 ? 63 : 64;
	auto conflict = client.sync(conflicting, "live-sync-conflict");
	r.check("sync_idempotency_conflict",
		!conflict.has_value() && conflict.error().error_code == "IDEMPOTENCY_CONFLICT");
	const auto before_state = request.value().expected_state_version;
	r.check("accept_sync", s.accept_sync(response.value()).has_value());
	r.check("delivery_saved", s.snapshot().delivery == StudyDelivery::saved);
	StudySyncRequest stale;
	stale.sync_id = new_study_uuid();
	stale.session_id = request.value().session_id;
	stale.client_stream_id = new_study_uuid();
	stale.expected_state_version = before_state;
	auto stale_result = client.sync(stale, "live-sync-stale");
	r.check("stale_state_version_conflict",
		!stale_result.has_value() && stale_result.error().error_code == "STUDY_STATE_STALE");
	r.fact("state_version_after", response.value().session.state_version);
	return r.finish();
}

int auth_failure(const Options &o, HttpLexinetClient &client)
{
	Report r("auth_failure");
	FlashCardSession s(open_store(o));
	r.check("restore", s.restore().has_value());
	auto create = client.create_or_resume(fresh_resume_request(o), "live-auth-create");
	r.check("create_unauthorized",
		!create.has_value() && create.error().error_code == "UNAUTHORIZED" &&
		    !create.error().retryable);
	// A rating auto-advances, so the second card is already presented.
	r.check("flip", act(s, StudyIntent::flip));
	r.check("rate_hard", act(s, StudyIntent::rate_hard));
	auto request = s.prepare_sync();
	if (!r.check("prepare_sync", request.has_value()))
		return r.finish();
	auto sync = client.sync(request.value(), "live-auth-sync");
	r.check("sync_unauthorized",
		!sync.has_value() && sync.error().error_code == "UNAUTHORIZED" &&
		    !sync.error().retryable);
	r.check("events_retained", !s.pending_events().empty());
	r.fact("pending_sync_id", request.value().sync_id);
	r.fact("pending_event_count", s.pending_events().size());
	return r.finish();
}

int offline_queue(const Options &o, HttpLexinetClient &client)
{
	Report r("offline_queue");
	FlashCardSession s(open_store(o));
	r.check("restore", s.restore().has_value());
	r.check("outbox_survived_restart", !s.pending_events().empty());
	auto request = s.prepare_sync();
	if (!r.check("prepare_sync", request.has_value()))
		return r.finish();
	r.check("durable_sync_id_reused",
		o.expect_sync_id.empty() || request.value().sync_id == o.expect_sync_id);
	auto sync = client.sync(request.value(), "live-offline-sync");
	if (!sync.has_value())
		r.error("sync_error", sync.error());
	r.check("transport_failure_is_retryable", !sync.has_value() && sync.error().retryable);
	s.set_connectivity(StudyConnectivity::offline);
	r.check("snapshot_offline", s.snapshot().connectivity == StudyConnectivity::offline);
	r.check("events_retained", !s.pending_events().empty());
	r.fact("pending_sync_id", request.value().sync_id);
	r.fact("pending_event_count", s.pending_events().size());
	r.fact("delivery", study_delivery_name(s.snapshot().delivery));
	return r.finish();
}

int recover(const Options &o, HttpLexinetClient &client)
{
	Report r("recover");
	FlashCardSession s(open_store(o));
	r.check("restore", s.restore().has_value());
	r.check("outbox_survived_restart", !s.pending_events().empty());
	s.set_connectivity(StudyConnectivity::online);
	deliver(s, client, r, "recover", o.expect_sync_id);
	r.check("delivery_saved", s.snapshot().delivery == StudyDelivery::saved);
	return r.finish();
}

int crash_after_send(const Options &o, HttpLexinetClient &client)
{
	Report r("crash_after_send");
	FlashCardSession s(open_store(o));
	r.check("restore", s.restore().has_value());
	r.check("third_card_active", s.snapshot().availability == StudyAvailability::ready);
	r.check("flip", act(s, StudyIntent::flip));
	r.check("rate_good", act(s, StudyIntent::rate_good));
	r.fact("view_after_rating", view_of(s));
	auto request = s.prepare_sync();
	if (!r.check("prepare_sync", request.has_value()))
		return r.finish();
	auto sync = client.sync(request.value(), "live-crash-sync");
	if (!r.check("sync_committed_before_crash", sync.has_value()))
		return r.finish();
	r.fact("pending_sync_id", request.value().sync_id);
	r.fact("flush_receipt_sha256", sha256_hex(sync.value().flush_receipt_json));
	r.finish();
	// Lose the response: the durable outbox still holds the committed request.
	std::fflush(nullptr);
	_exit(r.ok() ? kCrashExit : 1);
}

int resume_after_server_restart(const Options &o, HttpLexinetClient &client)
{
	Report r("resume_after_server_restart");
	FlashCardSession s(open_store(o));
	r.check("restore", s.restore().has_value());
	auto resumed = client.create_or_resume(fresh_resume_request(o), "live-restart-resume");
	if (!resumed.has_value())
		r.error("resume_error", resumed.error());
	r.check("server_state_survived_restart",
		resumed.has_value() &&
		    (o.expect_session.empty() || resumed.value().session_id == o.expect_session));
	if (resumed.has_value())
		r.fact("state_version", resumed.value().state_version);
	// A new write after the restart: touch Undo voids the last effective rating.
	r.check("undo_available", s.snapshot().can_undo);
	r.check("rating_undo", act(s, StudyIntent::rating_undo));
	r.fact("view_after_undo", view_of(s));
	deliver(s, client, r, "restart");
	r.check("delivery_saved", s.snapshot().delivery == StudyDelivery::saved);
	return r.finish();
}

int exhaust_and_successor(const Options &o, HttpLexinetClient &client)
{
	Report r("exhaust_and_successor");
	FlashCardSession s(open_store(o));
	r.check("restore", s.restore().has_value());
	const auto predecessor = s.snapshot().session_id;
	// Rate every remaining card, then advance past the last one; the reducer then
	// waits for the server-authoritative refill.
	for (int guard = 0; guard < 64 && s.snapshot().availability == StudyAvailability::ready;
	     ++guard) {
		const auto &view = s.snapshot();
		const auto intent = view.face == CardFace::front ? StudyIntent::flip
				    : view.can_rate		 ? StudyIntent::rate_easy
								 : StudyIntent::next;
		if (!act(s, intent))
			break;
	}
	deliver(s, client, r, "exhaust");
	r.fact("view_after_exhaustion", view_of(s));
	auto successor = s.prepare_successor();
	if (!successor.has_value()) {
		r.error("prepare_successor_error", successor.error());
		r.check("prepare_successor", false);
		return r.finish();
	}
	r.check("prepare_successor", true);
	auto created = client.create_or_resume(successor.value(), "live-successor");
	if (!created.has_value()) {
		r.error("successor_error", created.error());
		r.check("successor_created", false);
		return r.finish();
	}
	r.check("successor_created", true);
	r.check("successor_is_new_session", created.value().session_id != predecessor);
	r.check("successor_links_predecessor",
		created.value().predecessor_session_id == predecessor);
	auto replay = client.create_or_resume(successor.value(), "live-successor-replay");
	r.check("successor_replay_same_session",
		replay.has_value() && replay.value().session_id == created.value().session_id);
	r.check("install_successor", s.install_session(created.value()).has_value());
	deliver(s, client, r, "successor");
	r.fact("predecessor_session_id", predecessor);
	r.fact("successor_session_id", created.value().session_id);
	return r.finish();
}

int atomic_rejection(const Options &o, HttpLexinetClient &client)
{
	Report r("atomic_rejection");
	auto session = client.create_or_resume(fresh_resume_request(o), "live-atomic-resume");
	if (!r.check("resume_active_session", session.has_value()) ||
	    !r.check("two_items_available", session.value().cards.size() >= 2))
		return r.finish();
	const auto &items = session.value().cards;
	StudySyncRequest request;
	request.sync_id = new_study_uuid();
	request.session_id = session.value().session_id;
	request.client_stream_id = new_study_uuid();
	request.expected_state_version = session.value().state_version;
	request.limit = 1;
	for (std::size_t index = 0; index < 2; ++index) {
		StudyEvent event;
		event.event_id = new_study_uuid();
		event.client_sequence = index + 1;
		event.session_id = request.session_id;
		event.session_item_id = items[index].session_item_id;
		event.card_identity = items[index].vocabulary_card_id;
		event.profile_ref = o.profile_ref;
		event.event_kind = "presented";
		// The second event names a content revision the server never issued.
		event.content_sha256 = index == 0 ? items[index].content_sha256
						  : std::string(64, 'a');
		request.events.push_back(event);
	}
	auto response = client.sync(request, "live-atomic-sync");
	if (!response.has_value()) {
		r.error("sync_error", response.error());
		r.check("suffix_adjudicated", false);
		return r.finish();
	}
	const auto &receipts = response.value().receipts;
	bool all_rejected = receipts.size() == 2;
	for (const auto &receipt : receipts)
		all_rejected = all_rejected && receipt.disposition == "terminal_rejected" &&
			       receipt.code == receipts.front().code;
	r.check("whole_suffix_terminal_rejected", all_rejected);
	if (!receipts.empty())
		r.fact("rejection_code", receipts.front().code);
	r.check("no_canonical_event_ingested",
		response.value().profile_ingestion_high_water ==
		    session.value().profile_ingestion_high_water);
	r.check("stream_positions_consumed", response.value().adjudicated_through_sequence == 2);
	auto replay = client.sync(request, "live-atomic-replay");
	r.check("terminal_receipts_replay_identically",
		replay.has_value() && replay.value().receipts.size() == receipts.size() &&
		    replay.value().flush_receipt_json == response.value().flush_receipt_json);
	return r.finish();
}
} // namespace

int main(int argc, char **argv)
{
	const auto options = parse(argc, argv);
	auto created_runtime = bhp::http_runtime::HttpRuntime::create();
	if (!created_runtime.has_value())
		usage("HTTP runtime unavailable");
	auto runtime = created_runtime.value();
	int status = 2;
	{
		bhp::StructuredLogger logger("test.study_live_lexinet_contract", std::cerr);
		HttpLexinetClient client(runtime, client_config(options), logger);
		if (options.scenario == "create_resume")
			status = create_resume(options, client);
		else if (options.scenario == "study_and_sync")
			status = study_and_sync(options, client);
		else if (options.scenario == "auth_failure")
			status = auth_failure(options, client);
		else if (options.scenario == "offline_queue")
			status = offline_queue(options, client);
		else if (options.scenario == "recover")
			status = recover(options, client);
		else if (options.scenario == "crash_after_send")
			status = crash_after_send(options, client);
		else if (options.scenario == "resume_after_server_restart")
			status = resume_after_server_restart(options, client);
		else if (options.scenario == "exhaust_and_successor")
			status = exhaust_and_successor(options, client);
		else if (options.scenario == "atomic_rejection")
			status = atomic_rejection(options, client);
		else
			usage("unknown scenario " + options.scenario);
	}
	if (!runtime->shutdown().has_value())
		status = 1;
	return status;
}
