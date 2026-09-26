#include "fixtures/flash_card_fixture.h"
#include "vocabulary_panel/fixture_lexinet_client.h"
#include "vocabulary_panel/flash_card_session.h"
#include "vocabulary_panel/study_wire_codec.h"
#include <algorithm>
#include <cerrno>
#include <filesystem>
#include <iostream>
#include <nlohmann/json.hpp>
#include <unistd.h>
using namespace vocabulary_panel;
namespace
{
int failures = 0, checks = 0;
void expect(bool condition, const char *scenario)
{
	++checks;
	if (!condition) {
		++failures;
		std::cerr << "FAILED: " << scenario << '\n';
	}
}
VocabularyPanelResult<FlashCardSnapshot> act(FlashCardSession &s, StudyIntent intent,
					     StudyInputSource source = StudyInputSource::touch)
{
	return s.apply_intent(intent, source, s.current_binding());
}
void install(FlashCardSession &s)
{
	expect(
	    s.install_session(test_fixture::three_cards(), test_fixture::session_id()).has_value(),
	    "fixture session installs");
}
void test_two_faces_and_face_preserving_bounded_history()
{
	StudyBounds bounds;
	bounds.maximum_history = 2;
	FlashCardSession s(std::make_shared<MemoryStudyStore>(), bounds);
	install(s);
	expect(s.snapshot().face == CardFace::front, "new card front");
	act(s, StudyIntent::flip);
	expect(s.snapshot().can_rate && s.snapshot().face == CardFace::back,
	       "one flip reveals full back and ratings");
	act(s, StudyIntent::next);
	act(s, StudyIntent::back);
	expect(s.snapshot().face == CardFace::back, "back preserves previous face");
	act(s, StudyIntent::flip);
	expect(s.snapshot().face == CardFace::front, "flip toggles back to front");
	act(s, StudyIntent::next);
	act(s, StudyIntent::next);
	act(s, StudyIntent::back);
	expect(s.snapshot().history_size == 2 && !s.snapshot().can_back,
	       "bounded browser history evicts only presentation indices");
}
void test_stale_binding_and_front_rating_rejected()
{
	FlashCardSession s;
	install(s);
	auto binding = s.current_binding();
	expect(!act(s, StudyIntent::rate_good).has_value(), "front cannot rate");
	act(s, StudyIntent::flip);
	expect(!s.apply_intent(StudyIntent::next, StudyInputSource::touch, binding).has_value(),
	       "stale exact binding rejected");
}
void test_touch_rating_undo_is_append_only_and_leaves_successor()
{
	FlashCardSession s;
	install(s);
	act(s, StudyIntent::flip);
	expect(act(s, StudyIntent::rate_good).has_value(), "rating commits");
	const auto successor = s.snapshot().flash_card->session_item_id;
	const auto rating = s.pending_events()[2].event_id;
	expect(s.pending_events().size() == 4 && s.snapshot().can_undo,
	       "rating plus successor fact and reservation");
	expect(!act(s, StudyIntent::rating_undo, StudyInputSource::voice).has_value(),
	       "Undo is touch-only");
	expect(act(s, StudyIntent::rating_undo).has_value(), "touch Undo accepted");
	expect(s.pending_events().back().event_kind == "rating_voided" &&
		   s.pending_events().back().target_rating_event_id == rating,
	       "Undo targets reducer lease exact rating UUID");
	expect(s.snapshot().flash_card->session_item_id == successor && !s.snapshot().can_undo,
	       "Undo preserves already visible successor");
}
void test_capacity_reserves_undo_and_pauses_touch_successor()
{
	StudyBounds bounds;
	bounds.maximum_pending_events = 4;
	FlashCardSession s(std::make_shared<MemoryStudyStore>(), bounds);
	install(s);
	act(s, StudyIntent::flip);
	expect(act(s, StudyIntent::rate_easy).has_value(), "base rating plus Undo fits");
	expect(s.snapshot().availability == StudyAvailability::refill_required &&
		   s.snapshot().tracking_paused && s.snapshot().can_undo,
	       "touch successor needing extra slot remains unseen");
	expect(act(s, StudyIntent::rating_undo).has_value() && s.pending_events().size() == 4,
	       "reserved correction remains writable at bound");
}
void test_failed_commit_leaves_event_ids_history_and_rating_unchanged()
{
	auto store = std::make_shared<MemoryStudyStore>();
	FlashCardSession s(store);
	install(s);
	act(s, StudyIntent::flip);
	auto before = s.snapshot();
	auto events = s.pending_events();
	store->fail_next_commit = true;
	expect(!act(s, StudyIntent::rate_again).has_value(), "write failure rejects rating");
	expect(s.snapshot().flash_card->session_item_id == before.flash_card->session_item_id &&
		   s.snapshot().can_rate && !s.snapshot().can_undo &&
		   s.pending_events().size() == events.size(),
	       "failed atomic commit changes no canonical state");
}
void test_voice_first_observation_provisional_and_restart_resume_gate()
{
	auto store = std::make_shared<MemoryStudyStore>();
	{
		FlashCardSession s(store);
		install(s);
		act(s, StudyIntent::next, StudyInputSource::voice);
		act(s, StudyIntent::flip, StudyInputSource::voice);
		expect(s.pending_events().size() == 1 && s.snapshot().provisional,
		       "voice first presentation/reveal has no sequence or queued fact");
	}
	FlashCardSession restored(store);
	expect(restored.restore().has_value(), "restore succeeds");
	expect(restored.snapshot().availability == StudyAvailability::resume_required &&
		   !restored.snapshot().flash_card,
	       "provisional render discarded into non-card resume gate");
	expect(act(restored, StudyIntent::resume).has_value() &&
		   restored.pending_events().size() == 3 && !restored.snapshot().provisional,
	       "touch resume durably presents and reveals exact retained item");
}
void test_voice_rating_promotes_current_but_successor_stays_provisional()
{
	FlashCardSession s;
	install(s);
	act(s, StudyIntent::next, StudyInputSource::voice);
	act(s, StudyIntent::flip, StudyInputSource::voice);
	expect(act(s, StudyIntent::rate_hard, StudyInputSource::voice).has_value(),
	       "voice rating accepts revealed provisional item");
	expect(s.pending_events().size() == 4 && s.pending_events()[1].event_kind == "presented" &&
		   s.pending_events()[2].event_kind == "revealed" &&
		   s.pending_events()[3].event_kind == "rated",
	       "rating atomically promotes both prerequisites");
	expect(s.snapshot().provisional && s.snapshot().can_undo,
	       "voice rating successor remains provisional with durable Undo");
}
void test_voice_prerequisites_counted_in_preflight()
{
	StudyBounds b;
	b.maximum_pending_events = 4;
	FlashCardSession s(std::make_shared<MemoryStudyStore>(), b);
	install(s);
	act(s, StudyIntent::next, StudyInputSource::voice);
	act(s, StudyIntent::flip, StudyInputSource::voice);
	auto id = s.snapshot().flash_card->session_item_id;
	expect(!act(s, StudyIntent::rate_good, StudyInputSource::voice).has_value(),
	       "insufficient promotion plus rating plus Undo fails");
	expect(s.pending_events().size() == 1 && s.snapshot().flash_card->session_item_id == id &&
		   s.snapshot().provisional,
	       "rejected preflight preserves provisional state and capacity");
}
void test_acknowledgement_preserves_reservation_and_uuid_replay()
{
	FlashCardSession s;
	install(s);
	act(s, StudyIntent::flip);
	act(s, StudyIntent::rate_easy);
	auto request = s.prepare_sync();
	auto replay = s.prepare_sync();
	expect(request.has_value() && request.value().sync_id == replay.value().sync_id,
	       "retry reuses durable sync UUID");
	FixtureLexinetClient client(test_fixture::three_cards());
	auto result = client.sync(request.value(), "fixture");
	expect(s.accept_sync(result.value()).has_value() && s.pending_events().empty() &&
		   s.snapshot().can_undo,
	       "ack removes events but retains Undo reservation");
	expect(act(s, StudyIntent::rating_undo).has_value() && s.pending_events().size() == 1,
	       "acknowledged rating remains correctable");
}
void test_grouped_terminal_rejection_preserves_visible_successor()
{
	auto store = std::make_shared<MemoryStudyStore>();
	{
		FlashCardSession s(store);
		install(s);
		act(s, StudyIntent::flip);
		act(s, StudyIntent::rate_easy);
		const auto shown = s.snapshot().flash_card->session_item_id;
		FixtureLexinetClient client(test_fixture::three_cards());
		client.terminal_rejection = true;
		const auto request = s.prepare_sync();
		const auto response = client.sync(request.value(), "fixture");
		expect(s.accept_sync(response.value()).has_value(),
		       "stable terminal suffix receipt commits");
		expect(s.snapshot().delivery == StudyDelivery::rejected && !s.snapshot().can_undo &&
			   s.pending_events().empty() &&
			   s.snapshot().flash_card->session_item_id == shown,
		       "rejected group preserves rendered successor and releases reservation "
		       "without void");
		const auto count = s.pending_events().size();
		expect(!act(s, StudyIntent::flip).has_value() && s.pending_events().size() == count,
		       "rejected prerequisite cannot be reissued under new identity");
	}
	FlashCardSession s(store);
	expect(s.restore().has_value() && s.snapshot().delivery == StudyDelivery::rejected &&
		   s.snapshot().flash_card.has_value(),
	       "content-free rejection and visible state survive restart");
}
void test_final_card_completion_retains_undo()
{
	FlashCardSession s;
	expect(s.install_session({test_fixture::valid_flash_card()}, test_fixture::session_id())
		   .has_value(),
	       "single card install");
	act(s, StudyIntent::flip);
	act(s, StudyIntent::rate_good);
	expect(s.snapshot().availability == StudyAvailability::refill_required &&
		   s.snapshot().can_undo,
	       "final card goes to stable completion with Undo");
}
void test_flush_receipt_barrier_and_corruption_fail_visible()
{
	auto store = std::make_shared<MemoryStudyStore>();
	FlashCardSession s(store);
	install(s);
	expect(!s.predecessor_flush_receipt().has_value(),
	       "pending predecessor blocks successor barrier");
	FixtureLexinetClient client(test_fixture::three_cards());
	auto request = s.prepare_sync();
	s.accept_sync(client.sync(request.value(), "fixture").value());
	expect(s.predecessor_flush_receipt().has_value(),
	       "fresh acknowledged flush receipt persisted");
	act(s, StudyIntent::flip);
	expect(!s.predecessor_flush_receipt().has_value(),
	       "new local event invalidates old flush barrier");
	store->commit("{broken");
	FlashCardSession broken(store);
	expect(!broken.restore().has_value() &&
		   broken.snapshot().availability == StudyAvailability::error,
	       "corrupt cache fails visibly instead of empty");
}
void test_owner_only_file_restart_lock_and_bounds()
{
	const auto path =
	    std::filesystem::temp_directory_path() / ("bhp-study-test-" + new_study_uuid());
	std::string stream;
	{
		auto store = std::make_shared<FileStudyStore>(path.string());
		FlashCardSession s(store);
		install(s);
		act(s, StudyIntent::flip);
		act(s, StudyIntent::rate_hard);
		stream = s.client_stream_id();
		FileStudyStore concurrent(path.string());
		expect(!concurrent.read().has_value(), "second process owner lock refused");
		expect(s.snapshot().durable_storage, "filesystem storage labelled durable");
	}
	{
		auto store = std::make_shared<FileStudyStore>(path.string());
		FlashCardSession s(store);
		expect(s.restore().has_value() && s.client_stream_id() == stream &&
			   s.snapshot().can_undo,
		       "stream sequence and Undo survive disk restart");
		expect(act(s, StudyIntent::rating_undo).has_value() &&
			   s.pending_events().back().client_sequence == 5,
		       "restored reservation consumed at next contiguous sequence");
	}
	{
		FileStudyStore bounded(path.string(), 1);
		expect(!bounded.read().has_value() && !bounded.commit("long").has_value(),
		       "byte bounds checked on read and write");
	}
	std::filesystem::remove_all(path);
}
void test_bounded_cache_eviction_preserves_pending_and_history()
{
	StudyBounds b;
	b.maximum_cards = 3;
	b.maximum_history = 1;
	b.prefetch_low_water = 1;
	FlashCardSession s(std::make_shared<MemoryStudyStore>(), b);
	install(s);
	act(s, StudyIntent::next);
	act(s, StudyIntent::next);
	auto request = s.prepare_sync();
	FixtureLexinetClient client(test_fixture::three_cards());
	auto response = client.sync(request.value(), "fixture").value();
	auto card = test_fixture::valid_flash_card();
	card.position = 3;
	card.session_item_id = "22222222-2222-4222-8222-222222222225";
	response.session.cards = {card};
	expect(s.accept_sync(response).has_value(),
	       "acknowledged card outside bounded history can be evicted for refill");
	expect(act(s, StudyIntent::next).has_value() && s.snapshot().flash_card->position == 3,
	       "bounded refill retains canonical session order");
	FlashCardSession protected_session(std::make_shared<MemoryStudyStore>(), b);
	install(protected_session);
	auto prior = protected_session.prepare_sync();
	act(protected_session, StudyIntent::flip);
	act(protected_session, StudyIntent::next);
	act(protected_session, StudyIntent::next);
	FixtureLexinetClient other(test_fixture::three_cards());
	auto late = other.sync(prior.value(), "fixture").value();
	late.session.cards = {card};
	expect(protected_session.accept_sync(late).has_value(),
	       "acknowledgement commits while raced refill is deferred");
	expect(protected_session.pending_events().size() == 3 &&
		   protected_session.snapshot().tracking_paused,
	       "unacknowledged card snapshots never evicted by raced refill");
}
void test_inflight_identity_survives_restart_and_local_undo_wins_barrier()
{
	auto store = std::make_shared<MemoryStudyStore>();
	std::string id, event;
	{
		FlashCardSession s(store);
		install(s);
		act(s, StudyIntent::flip);
		auto r = s.prepare_sync();
		id = r.value().sync_id;
		event = r.value().events.back().event_id;
	}
	FlashCardSession restored(store);
	expect(restored.restore().has_value(), "inflight restore succeeds");
	auto replay = restored.prepare_sync();
	expect(replay.value().sync_id == id && replay.value().events.back().event_id == event,
	       "process restart retains exact sync and event idempotency identities");
	FlashCardSession s;
	expect(s.install_session({test_fixture::valid_flash_card()}, test_fixture::session_id())
		   .has_value(),
	       "barrier single card installs");
	act(s, StudyIntent::flip);
	act(s, StudyIntent::rate_good);
	FixtureLexinetClient client(std::vector<FlashCard>{test_fixture::valid_flash_card()});
	auto r = s.prepare_sync();
	s.accept_sync(client.sync(r.value(), "fixture").value());
	act(s, StudyIntent::rating_undo);
	expect(!s.prepare_successor().has_value(),
	       "Undo accepted before successor barrier must flush first");
	r = s.prepare_sync();
	s.accept_sync(client.sync(r.value(), "fixture").value());
	auto successor = s.prepare_successor();
	expect(successor.has_value() && s.prepare_successor().value().creation_request_id ==
					    successor.value().creation_request_id,
	       "fresh successor barrier retains one durable creation identity");
}

void test_initial_creation_is_durable_and_scope_changes_fail_closed()
{
	auto store = std::make_shared<MemoryStudyStore>();
	std::string original;
	{
		FlashCardSession s(store);
		store->fail_next_commit = true;
		expect(!s.prepare_create("fixture-default", {"fixture-systems", "fixture-words"})
			    .has_value(),
		       "initial HTTPS request unavailable when persistence fails");
		expect(!store->read().value().has_value(),
		       "failed initial preparation leaves no plausible durable request");
		auto prepared =
		    s.prepare_create("fixture-default", {"fixture-systems", "fixture-words"});
		expect(prepared.has_value(), "initial create request persists before exposure");
		original = encode_study_create(prepared.value()).value();
	}
	FlashCardSession resumed(store);
	expect(resumed.restore().has_value(),
	       "pending initial creation restores without any session");
	auto replay =
	    resumed.prepare_create("fixture-default", {"fixture-systems", "fixture-words"});
	expect(replay.has_value() && encode_study_create(replay.value()).value() == original,
	       "initial create UUID and complete wire body exactly survive restart");
	expect(!resumed.prepare_create("fixture-default", {"different-topic"}).has_value(),
	       "changed scope cannot replace durable initial create request");
}

void test_history_boundaries_restore_last_face_without_future_action()
{
	FlashCardSession s;
	install(s);
	const auto first = s.snapshot().flash_card->session_item_id;
	expect(act(s, StudyIntent::back).has_value() && !s.snapshot().error &&
		   s.snapshot().flash_card->session_item_id == first &&
		   s.pending_events().size() == 1,
	       "first Back is a visible no-op without error or learning fact");
	act(s, StudyIntent::next);
	act(s, StudyIntent::next);
	act(s, StudyIntent::flip);
	const auto last = s.snapshot().flash_card->session_item_id;
	const auto facts = s.pending_events().size();
	act(s, StudyIntent::next);
	expect(s.snapshot().availability == StudyAvailability::refill_required &&
		   s.snapshot().can_back,
	       "completion can navigate back to retained history");
	expect(act(s, StudyIntent::next).has_value() && !s.snapshot().error &&
		   s.snapshot().availability == StudyAvailability::refill_required &&
		   s.pending_events().size() == facts,
	       "depleted Next queues no future navigation or rating");
	expect(act(s, StudyIntent::back).has_value() && s.snapshot().flash_card &&
		   s.snapshot().flash_card->session_item_id == last &&
		   s.snapshot().face == CardFace::back && s.pending_events().size() == facts,
	       "Back from completion restores last face without new fact");
}
void test_effective_rating_replacement_requires_void_and_reuses_forward_history()
{
	FlashCardSession s;
	install(s);
	act(s, StudyIntent::flip);
	act(s, StudyIntent::next);
	act(s, StudyIntent::back);
	const auto item = s.snapshot().flash_card->session_item_id;
	expect(act(s, StudyIntent::rate_good).has_value() && s.pending_events().size() == 4,
	       "rating reuses already-presented forward history without duplicate presentation");
	const auto original = s.pending_events().back().event_id;
	act(s, StudyIntent::rating_undo);
	act(s, StudyIntent::back);
	expect(s.snapshot().flash_card->session_item_id == item && s.snapshot().can_rate &&
		   act(s, StudyIntent::rate_hard).has_value(),
	       "void permits one replacement effective rating");
	const auto count = s.pending_events().size();
	act(s, StudyIntent::back);
	expect(!s.snapshot().can_rate && !act(s, StudyIntent::rate_easy).has_value() &&
		   s.pending_events().size() == count,
	       "effective replacement cannot be rated again without void");
	expect(std::count_if(s.pending_events().begin(), s.pending_events().end(),
			     [](const auto &e) { return e.event_kind == "rated"; }) == 2 &&
		   s.pending_events()[4].target_rating_event_id == original,
	       "append-only stream has first rating, its exact void, and one replacement");
}
void test_full_outbox_provisional_history_requires_capacity_for_touch_promotion()
{
	StudyBounds b;
	b.maximum_pending_events = 2;
	auto store = std::make_shared<MemoryStudyStore>();
	FlashCardSession s(store, b);
	install(s);
	act(s, StudyIntent::flip);
	expect(!act(s, StudyIntent::next).has_value() && s.pending_events().size() == 2,
	       "full outbox refuses unseen touch presentation");
	act(s, StudyIntent::next, StudyInputSource::voice);
	act(s, StudyIntent::flip, StudyInputSource::voice);
	const auto provisional = s.snapshot().flash_card->session_item_id;
	expect(s.snapshot().provisional && s.snapshot().face == CardFace::back &&
		   s.pending_events().size() == 2,
	       "full outbox permits visibly provisional voice observations only");
	expect(!act(s, StudyIntent::flip).has_value() &&
		   !act(s, StudyIntent::rate_good).has_value(),
	       "touch promotion and rating reject atomically when outbox full");
	act(s, StudyIntent::back, StudyInputSource::voice);
	expect(
	    !act(s, StudyIntent::next).has_value() &&
		s.snapshot().flash_card->session_item_id != provisional,
	    "touch forward history cannot expose provisional target without durable prerequisites");
	FixtureLexinetClient client(test_fixture::three_cards());
	auto request = s.prepare_sync();
	s.accept_sync(client.sync(request.value(), "full-outbox").value());
	expect(act(s, StudyIntent::next).has_value() && !s.snapshot().provisional &&
		   s.snapshot().flash_card->session_item_id == provisional &&
		   s.pending_events().size() == 2 &&
		   s.pending_events()[0].event_kind == "presented" &&
		   s.pending_events()[1].event_kind == "revealed",
	       "touch restores exact history face only after both prerequisite facts commit");
	act(s, StudyIntent::next, StudyInputSource::voice);
	act(s, StudyIntent::flip, StudyInputSource::voice);
	expect(s.leave_surface().has_value() && !s.snapshot().flash_card &&
		   s.snapshot().availability == StudyAvailability::resume_required,
	       "surface termination discards provisional render into non-card gate");
	FlashCardSession restarted(store, b);
	restarted.restore();
	expect(restarted.snapshot().availability == StudyAvailability::resume_required &&
		   !restarted.snapshot().flash_card && restarted.pending_events().size() == 2,
	       "discarded provisional facts never acquire sequence or survive restart as card");
}
void test_duplicate_acknowledgement_and_same_batch_void_preserve_stream()
{
	FlashCardSession s;
	install(s);
	act(s, StudyIntent::flip);
	act(s, StudyIntent::rate_good);
	act(s, StudyIntent::rating_undo);
	auto request = s.prepare_sync();
	FixtureLexinetClient client(test_fixture::three_cards());
	auto response = client.sync(request.value(), "same-batch").value();
	expect(s.accept_sync(response).has_value() && s.pending_events().empty(),
	       "same batch acknowledges presentation reveal rating successor and exact void");
	const auto generation = s.snapshot().render_generation;
	expect(!s.accept_sync(response).has_value() &&
		   s.snapshot().render_generation == generation && s.pending_events().empty(),
	       "duplicate local acknowledgement is non-destructive typed rejection");
	act(s, StudyIntent::flip);
	const auto newest = s.pending_events().back().event_id;
	expect(!s.accept_sync(response).has_value() && s.pending_events().back().event_id == newest,
	       "old acknowledgement cannot remove newer queued evidence");
	expect(s.pending_events().back().client_sequence == 6,
	       "acknowledged stream sequence remains contiguous");
	FlashCardSession reprovisioned;
	expect(reprovisioned.client_stream_id() != s.client_stream_id(),
	       "reprovisioning creates a distinct stream UUID");
}
void test_released_undo_and_empty_sentinel_remain_stable_after_restart()
{
	auto store = std::make_shared<MemoryStudyStore>();
	{
		FlashCardSession s(store);
		install(s);
		act(s, StudyIntent::flip);
		act(s, StudyIntent::rate_good);
		expect(s.leave_surface().has_value() && !s.snapshot().can_undo,
		       "surface exit releases correction reservation");
	}
	FlashCardSession restarted(store);
	restarted.restore();
	expect(!restarted.snapshot().can_undo &&
		   !act(restarted, StudyIntent::rating_undo).has_value(),
	       "released lease never reappears after restart");
	auto empty_store = std::make_shared<MemoryStudyStore>();
	FixtureLexinetClient client(std::vector<FlashCard>{});
	FlashCardSession empty(empty_store);
	auto create = empty.prepare_create("fixture-default", {"fixture-systems", "fixture-words"});
	auto response = client.create_or_resume(create.value(), "empty").value();
	expect(empty.install_session(response).has_value() &&
		   empty.snapshot().availability == StudyAvailability::empty && !empty.needs_sync(),
	       "waiting-for-corpus sentinel remains empty without polling");
	FlashCardSession restored(empty_store);
	restored.restore();
	expect(restored.snapshot().session_id == response.session_id &&
		   restored.snapshot().availability == StudyAvailability::empty &&
		   !restored.needs_sync() && !restored.prepare_successor().has_value(),
	       "restart reuses the one empty sentinel without new cycle");
}
class DiskFullStore final : public StudyStore
{
      public:
	MemoryStudyStore storage;
	bool full{false};
	VocabularyPanelResult<std::optional<std::string>> read() override { return storage.read(); }
	VocabularyPanelResult<bool> commit(const std::string &payload) override
	{
		if (full)
			return VocabularyPanelResult<bool>::failure(
			    {"STUDY.STORAGE_FAILED",
			     "Injected disk full at persistence boundary",
			     {{"operation", "write_temp"}, {"errno", std::to_string(ENOSPC)}},
			     false});
		return storage.commit(payload);
	}
	bool durable() const override { return false; }
};
void test_disk_full_preserves_durable_payload_and_retryable_local_action()
{
	auto store = std::make_shared<DiskFullStore>();
	FlashCardSession s(store);
	install(s);
	act(s, StudyIntent::flip);
	const auto durable = store->read().value();
	const auto binding = s.current_binding();
	store->full = true;
	auto result = act(s, StudyIntent::rate_good);
	expect(!result.has_value() &&
		   result.error().error_context.at("errno") == std::to_string(ENOSPC) &&
		   s.current_binding().render_generation == binding.render_generation &&
		   store->read().value() == durable && !s.snapshot().can_undo,
	       "injected disk-full preserves exact committed payload and visible item");
	store->full = false;
	expect(act(s, StudyIntent::rate_good).has_value() &&
		   s.pending_events()[2].client_sequence == 3,
	       "recovered disk accepts one rating without leaked sequence or reservation");
}

} // namespace
int main()
{
	test_history_boundaries_restore_last_face_without_future_action();
	test_effective_rating_replacement_requires_void_and_reuses_forward_history();
	test_full_outbox_provisional_history_requires_capacity_for_touch_promotion();
	test_duplicate_acknowledgement_and_same_batch_void_preserve_stream();
	test_released_undo_and_empty_sentinel_remain_stable_after_restart();
	test_disk_full_preserves_durable_payload_and_retryable_local_action();
	test_initial_creation_is_durable_and_scope_changes_fail_closed();
	test_two_faces_and_face_preserving_bounded_history();
	test_stale_binding_and_front_rating_rejected();
	test_touch_rating_undo_is_append_only_and_leaves_successor();
	test_capacity_reserves_undo_and_pauses_touch_successor();
	test_failed_commit_leaves_event_ids_history_and_rating_unchanged();
	test_voice_first_observation_provisional_and_restart_resume_gate();
	test_voice_rating_promotes_current_but_successor_stays_provisional();
	test_voice_prerequisites_counted_in_preflight();
	test_acknowledgement_preserves_reservation_and_uuid_replay();
	test_grouped_terminal_rejection_preserves_visible_successor();
	test_final_card_completion_retains_undo();
	test_flush_receipt_barrier_and_corruption_fail_visible();
	test_owner_only_file_restart_lock_and_bounds();
	test_bounded_cache_eviction_preserves_pending_and_history();
	test_inflight_identity_survives_restart_and_local_undo_wins_barrier();
	std::cout << "flash_card_session: " << checks << " checks, " << failures << " failures\n";
	return failures ? 1 : 0;
}
