#include "panel_supervisor/touch_observation_authority.hpp"

#include <chrono>
#include <atomic>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using namespace house_panel::supervisor;
using namespace std::chrono_literals;

#define TEST_REQUIRE(condition)                                                                    \
	do {                                                                                         \
		if (!(condition)) {                                                                    \
			std::cerr << __func__ << ":" << __LINE__ << " requirement failed: " #condition \
				  << "\n";                                                           \
			return false;                                                                    \
		}                                                                                    \
	} while (false)

class FixtureMonotonicClock final {
public:
	[[nodiscard]] std::chrono::steady_clock::time_point now() const
	{
		if (throw_on_read_) {
			throw std::runtime_error("fixture clock failure");
		}
		return now_;
	}

	void advance(const std::chrono::milliseconds duration) { now_ += duration; }
	void set(const std::chrono::steady_clock::time_point now) { now_ = now; }
	void set_throw_on_read(const bool throw_on_read) { throw_on_read_ = throw_on_read; }

private:
	std::chrono::steady_clock::time_point now_{
		std::chrono::steady_clock::time_point{} + 10s};
	bool throw_on_read_{false};
};

class FixtureObservationIdGenerator final {
public:
	explicit FixtureObservationIdGenerator(std::vector<std::string> ids)
		: ids_(std::move(ids))
	{
	}

	[[nodiscard]] Result<std::string> next()
	{
		++call_count_;
		if (fail_) {
			return Result<std::string>::failure(SupervisorError(
				ErrorCode::AdapterUnavailable, "Fixture CSPRNG failed",
				{{"operation", "fixture_generate"}}));
		}
		if (next_index_ >= ids_.size()) {
			return Result<std::string>::failure(SupervisorError(
				ErrorCode::AdapterUnavailable, "Fixture CSPRNG exhausted",
				{{"operation", "fixture_generate"}}));
		}
		return Result<std::string>::success(ids_[next_index_++]);
	}

	void set_fail(const bool fail) { fail_ = fail; }
	[[nodiscard]] std::size_t call_count() const noexcept { return call_count_; }

private:
	std::vector<std::string> ids_;
	std::size_t next_index_{0};
	std::size_t call_count_{0};
	bool fail_{false};
};

TouchPresentationBinding fixture_presentation()
{
	return TouchPresentationBinding{
		2, "00112233445566778899aabbccddeeff", 11,
		"sha256:1111111111111111111111111111111111111111111111111111111111111111",
		"ffeeddccbbaa99887766554433221100", 7};
}

TouchObservationAuthorityConfiguration fixture_configuration(
	const std::size_t maximum_retained_observations = 4)
{
	return TouchObservationAuthorityConfiguration{1000ms, 10000ms,
					      maximum_retained_observations};
}

Result<std::unique_ptr<TouchObservationAuthority>> create_fixture_authority(
	FixtureMonotonicClock &clock, FixtureObservationIdGenerator &generator,
	const std::size_t maximum_retained_observations = 4)
{
	return TouchObservationAuthority::create(
		fixture_configuration(maximum_retained_observations),
		[&generator]() { return generator.next(); }, [&clock]() { return clock.now(); });
}

Result<TouchObservationReceipt> record_fixture_contact(
	TouchObservationAuthority &authority, FixtureMonotonicClock &clock,
	const std::chrono::milliseconds contact_duration)
{
	auto started = authority.record_contact_started();
	if (!started) {
		return Result<TouchObservationReceipt>::failure(started.error());
	}
	clock.advance(contact_duration);
	return authority.record_contact_completed();
}

TouchObservationClaim fixture_claim(const TouchObservationReceipt &receipt)
{
	return TouchObservationClaim{receipt.touch_observation_id,
				     "sha256:2222222222222222222222222222222222222222222222222222222222222222",
				     receipt.presentation_binding, std::nullopt};
}

bool has_reason(const SupervisorError &error, const TouchObservationErrorReason reason)
{
	const auto entry = error.context().find("reason");
	return entry != error.context().end() && entry->second == to_string(reason);
}

bool configuration_requires_explicit_bounded_ttl_and_storage()
{
	FixtureMonotonicClock clock;
	FixtureObservationIdGenerator generator({"observation-id-fixture-00000001"});
	const auto valid = TouchObservationAuthority::create(
		{5000ms, 10000ms, 1}, [&generator]() { return generator.next(); },
		[&clock]() { return clock.now(); });
	TEST_REQUIRE(valid);

	const auto excessive_ttl = TouchObservationAuthority::create(
		{5001ms, 10000ms, 1}, [&generator]() { return generator.next(); },
		[&clock]() { return clock.now(); });
	TEST_REQUIRE(!excessive_ttl);
	TEST_REQUIRE(has_reason(excessive_ttl.error(),
				TouchObservationErrorReason::InvalidConfiguration));

	const auto zero_storage = TouchObservationAuthority::create(
		{1000ms, 10000ms, 0}, [&generator]() { return generator.next(); },
		[&clock]() { return clock.now(); });
	TEST_REQUIRE(!zero_storage);
	return true;
}

bool contact_start_requires_current_committed_presentation()
{
	FixtureMonotonicClock clock;
	FixtureObservationIdGenerator generator({"observation-id-fixture-00000001"});
	auto authority = create_fixture_authority(clock, generator);
	TEST_REQUIRE(authority);
	const auto started = authority.value()->record_contact_started();
	TEST_REQUIRE(!started);
	TEST_REQUIRE(has_reason(started.error(),
				TouchObservationErrorReason::NoCommittedPresentation));
	TEST_REQUIRE(generator.call_count() == 0);
	return true;
}

bool contact_completion_requires_a_start_and_duplicate_start_cannot_reset_time()
{
	FixtureMonotonicClock clock;
	FixtureObservationIdGenerator generator({"observation-id-fixture-00000001"});
	auto authority = create_fixture_authority(clock, generator);
	TEST_REQUIRE(authority.value()->record_committed_presentation(fixture_presentation()));

	const auto without_start = authority.value()->record_contact_completed();
	TEST_REQUIRE(!without_start);
	TEST_REQUIRE(has_reason(without_start.error(),
				TouchObservationErrorReason::ContactNotActive));
	TEST_REQUIRE(generator.call_count() == 0);

	TEST_REQUIRE(authority.value()->record_contact_started());
	clock.advance(300ms);
	const auto duplicate_start = authority.value()->record_contact_started();
	TEST_REQUIRE(!duplicate_start);
	TEST_REQUIRE(has_reason(duplicate_start.error(),
				TouchObservationErrorReason::ContactAlreadyActive));
	clock.advance(500ms);
	const auto completed = authority.value()->record_contact_completed();
	TEST_REQUIRE(completed);
	TEST_REQUIRE(completed.value().supervisor_measured_contact_duration == 800ms);
	return true;
}

bool contact_duration_is_derived_from_authority_clock_and_bounded()
{
	FixtureMonotonicClock clock;
	FixtureObservationIdGenerator generator({"observation-id-fixture-00000001"});
	auto authority = create_fixture_authority(clock, generator);
	TEST_REQUIRE(authority.value()->record_committed_presentation(fixture_presentation()));

	TEST_REQUIRE(authority.value()->record_contact_started());
	const auto zero_duration = authority.value()->record_contact_completed();
	TEST_REQUIRE(!zero_duration);
	TEST_REQUIRE(has_reason(zero_duration.error(),
				TouchObservationErrorReason::ContactDurationInvalid));

	TEST_REQUIRE(authority.value()->record_contact_started());
	clock.advance(10001ms);
	const auto excessive_duration = authority.value()->record_contact_completed();
	TEST_REQUIRE(!excessive_duration);
	TEST_REQUIRE(has_reason(excessive_duration.error(),
				TouchObservationErrorReason::ContactDurationInvalid));
	TEST_REQUIRE(generator.call_count() == 0);
	return true;
}

bool protocol_bindings_and_operation_digest_use_exact_grammars()
{
	FixtureMonotonicClock clock;
	FixtureObservationIdGenerator generator({"observation-id-fixture-00000001"});
	auto authority = create_fixture_authority(clock, generator);
	auto invalid_binding = fixture_presentation();
	invalid_binding.supervisor_session_id = "00112233445566778899AABBCCDDEEFF";
	const auto invalid_presentation =
		authority.value()->record_committed_presentation(invalid_binding);
	TEST_REQUIRE(!invalid_presentation);
	TEST_REQUIRE(has_reason(invalid_presentation.error(),
				TouchObservationErrorReason::InvalidPresentationBinding));

	TEST_REQUIRE(authority.value()->record_committed_presentation(fixture_presentation()));
	const auto observation = record_fixture_contact(*authority.value(), clock, 500ms);
	TEST_REQUIRE(observation);
	auto invalid_claim = fixture_claim(observation.value());
	invalid_claim.canonical_operation_digest = "sha256:not-a-canonical-digest";
	const auto invalid_digest = authority.value()->redeem_for_media_broker(invalid_claim);
	TEST_REQUIRE(!invalid_digest);
	TEST_REQUIRE(has_reason(invalid_digest.error(),
				TouchObservationErrorReason::OperationDigestInvalid));
	TEST_REQUIRE(authority.value()->redeem_for_media_broker(fixture_claim(observation.value())));
	return true;
}

bool generated_observation_binds_complete_supervisor_evidence()
{
	FixtureMonotonicClock clock;
	FixtureObservationIdGenerator generator({"observation-id-fixture-00000001"});
	auto authority = create_fixture_authority(clock, generator);
	const auto presentation = fixture_presentation();
	TEST_REQUIRE(authority.value()->record_committed_presentation(presentation));
	const auto started_at = clock.now();
	const auto observation = record_fixture_contact(*authority.value(), clock, 750ms);
	const auto completed_at = started_at + 750ms;
	TEST_REQUIRE(observation);
	TEST_REQUIRE(observation.value().touch_observation_id ==
		     "observation-id-fixture-00000001");
	TEST_REQUIRE(observation.value().observation_sequence == 1);
	TEST_REQUIRE(observation.value().presentation_binding.supervisor_session_id ==
		     presentation.supervisor_session_id);
	TEST_REQUIRE(observation.value().presentation_binding.lifecycle_snapshot_id ==
		     presentation.lifecycle_snapshot_id);
	TEST_REQUIRE(observation.value().presentation_binding.ui_connection_challenge ==
		     presentation.ui_connection_challenge);
	TEST_REQUIRE(observation.value().presentation_binding.committed_render_generation == 7);
	TEST_REQUIRE(observation.value().contact_completed_at == completed_at);
	TEST_REQUIRE(observation.value().supervisor_measured_contact_duration == 750ms);
	TEST_REQUIRE(observation.value().expires_at == completed_at + 1000ms);
	return true;
}

bool successful_redemption_is_one_use_and_operation_bound()
{
	FixtureMonotonicClock clock;
	FixtureObservationIdGenerator generator({"observation-id-fixture-00000001"});
	auto authority = create_fixture_authority(clock, generator);
	TEST_REQUIRE(authority.value()->record_committed_presentation(fixture_presentation()));
	const auto observation = record_fixture_contact(*authority.value(), clock, 800ms);
	auto claim = fixture_claim(observation.value());
	claim.qualifying_hold_threshold = 500ms;
	const auto provenance = authority.value()->redeem_for_media_broker(claim);
	TEST_REQUIRE(provenance);
	TEST_REQUIRE(provenance.value().capability_holder_audience ==
		     TouchCapabilityHolderAudience::MediaBroker);
	TEST_REQUIRE(provenance.value().canonical_operation_digest ==
		     claim.canonical_operation_digest);
	TEST_REQUIRE(provenance.value().presentation_binding.lifecycle_event_sequence == 11);
	TEST_REQUIRE(provenance.value().supervisor_measured_contact_duration == 800ms);
	TEST_REQUIRE(provenance.value().qualifying_hold_threshold == 500ms);
	TEST_REQUIRE(provenance.value().qualifying_hold_satisfied);

	const auto repeated = authority.value()->redeem_for_media_broker(claim);
	TEST_REQUIRE(!repeated);
	TEST_REQUIRE(has_reason(repeated.error(),
				TouchObservationErrorReason::ObservationAlreadyConsumed));
	return true;
}

bool personal_context_redemption_is_role_bound_outside_the_claim()
{
	FixtureMonotonicClock clock;
	FixtureObservationIdGenerator generator({"observation-id-fixture-00000001"});
	auto authority = create_fixture_authority(clock, generator);
	TEST_REQUIRE(authority.value()->record_committed_presentation(fixture_presentation()));
	const auto observation = record_fixture_contact(*authority.value(), clock, 800ms);
	const auto provenance = authority.value()->redeem_for_personal_context_client(
		fixture_claim(observation.value()));
	TEST_REQUIRE(provenance);
	TEST_REQUIRE(provenance.value().capability_holder_audience ==
		     TouchCapabilityHolderAudience::PersonalContextClient);
	return true;
}

bool concurrent_redemption_has_exactly_one_winner()
{
	FixtureMonotonicClock clock;
	FixtureObservationIdGenerator generator({"observation-id-fixture-00000001"});
	auto authority = create_fixture_authority(clock, generator);
	TEST_REQUIRE(authority.value()->record_committed_presentation(fixture_presentation()));
	const auto observation = record_fixture_contact(*authority.value(), clock, 800ms);
	const auto claim = fixture_claim(observation.value());
	auto *const authority_ptr = authority.value().get();

	std::atomic<unsigned int> ready_count{0};
	std::atomic<bool> start{false};
	std::optional<Result<TouchProvenanceEnvelope>> first_result;
	std::optional<Result<TouchProvenanceEnvelope>> second_result;
	auto redeem = [&](std::optional<Result<TouchProvenanceEnvelope>> &result) {
		ready_count.fetch_add(1, std::memory_order_release);
		while (!start.load(std::memory_order_acquire)) {
			std::this_thread::yield();
		}
		result.emplace(authority_ptr->redeem_for_media_broker(claim));
	};
	std::thread first_thread(redeem, std::ref(first_result));
	std::thread second_thread(redeem, std::ref(second_result));
	while (ready_count.load(std::memory_order_acquire) != 2) {
		std::this_thread::yield();
	}
	start.store(true, std::memory_order_release);
	first_thread.join();
	second_thread.join();

	TEST_REQUIRE(first_result.has_value());
	TEST_REQUIRE(second_result.has_value());
	const unsigned int success_count = static_cast<unsigned int>(first_result->has_value()) +
					   static_cast<unsigned int>(second_result->has_value());
	TEST_REQUIRE(success_count == 1);
	const auto &failure = first_result->has_value() ? *second_result : *first_result;
	TEST_REQUIRE(has_reason(failure.error(),
				TouchObservationErrorReason::ObservationAlreadyConsumed));
	return true;
}

bool lifecycle_invalidation_does_not_wait_for_slow_identity_generation()
{
	FixtureMonotonicClock clock;
	std::atomic<bool> generator_entered{false};
	std::atomic<bool> release_generator{false};
	auto authority = TouchObservationAuthority::create(
		fixture_configuration(),
		[&]() {
			generator_entered.store(true, std::memory_order_release);
			while (!release_generator.load(std::memory_order_acquire)) {
				std::this_thread::yield();
			}
			return Result<std::string>::success(
				"observation-id-fixture-00000001");
		},
		[&clock]() { return clock.now(); });
	TEST_REQUIRE(authority);
	TEST_REQUIRE(authority.value()->record_committed_presentation(fixture_presentation()));
	auto *const authority_ptr = authority.value().get();
	TEST_REQUIRE(authority_ptr->record_contact_started());
	clock.advance(500ms);
	std::optional<Result<TouchObservationReceipt>> contact_result;
	std::thread contact_thread([&]() {
		contact_result.emplace(authority_ptr->record_contact_completed());
	});
	while (!generator_entered.load(std::memory_order_acquire)) {
		std::this_thread::yield();
	}

	// This call must complete while the generator thread still owns the authority mutex.
	authority_ptr->invalidate(TouchObservationInvalidation::SuspendPreparation);
	release_generator.store(true, std::memory_order_release);
	contact_thread.join();
	TEST_REQUIRE(contact_result.has_value());
	TEST_REQUIRE(!contact_result->has_value());
	TEST_REQUIRE(has_reason(contact_result->error(),
				TouchObservationErrorReason::ObservationInvalidated));
	TEST_REQUIRE(authority_ptr->available_observation_count() == 0);
	return true;
}

bool lifecycle_invalidation_cancels_an_active_contact_before_completion()
{
	FixtureMonotonicClock clock;
	FixtureObservationIdGenerator generator({"observation-id-fixture-00000001"});
	auto authority = create_fixture_authority(clock, generator);
	TEST_REQUIRE(authority.value()->record_committed_presentation(fixture_presentation()));
	TEST_REQUIRE(authority.value()->record_contact_started());
	clock.advance(500ms);

	authority.value()->invalidate(TouchObservationInvalidation::UiDisconnected);
	const auto completion = authority.value()->record_contact_completed();
	TEST_REQUIRE(!completion);
	TEST_REQUIRE(has_reason(completion.error(),
				TouchObservationErrorReason::ContactNotActive));
	TEST_REQUIRE(generator.call_count() == 0);
	return true;
}

bool presentation_mismatch_terminally_invalidates_observation()
{
	FixtureMonotonicClock clock;
	FixtureObservationIdGenerator generator({"observation-id-fixture-00000001"});
	auto authority = create_fixture_authority(clock, generator);
	TEST_REQUIRE(authority.value()->record_committed_presentation(fixture_presentation()));
	const auto observation = record_fixture_contact(*authority.value(), clock, 500ms);
	auto mismatched_claim = fixture_claim(observation.value());
	++mismatched_claim.expected_presentation_binding.committed_render_generation;
	const auto mismatch = authority.value()->redeem_for_media_broker(mismatched_claim);
	TEST_REQUIRE(!mismatch);
	TEST_REQUIRE(has_reason(mismatch.error(),
				TouchObservationErrorReason::PresentationMismatch));

	const auto retry = authority.value()->redeem_for_media_broker(fixture_claim(observation.value()));
	TEST_REQUIRE(!retry);
	TEST_REQUIRE(has_reason(retry.error(),
				TouchObservationErrorReason::ObservationInvalidated));
	return true;
}

bool insufficient_qualifying_hold_terminally_invalidates_observation()
{
	FixtureMonotonicClock clock;
	FixtureObservationIdGenerator generator({"observation-id-fixture-00000001"});
	auto authority = create_fixture_authority(clock, generator);
	TEST_REQUIRE(authority.value()->record_committed_presentation(fixture_presentation()));
	const auto observation = record_fixture_contact(*authority.value(), clock, 500ms);
	auto claim = fixture_claim(observation.value());
	claim.qualifying_hold_threshold = 600ms;
	const auto insufficient = authority.value()->redeem_for_media_broker(claim);
	TEST_REQUIRE(!insufficient);
	TEST_REQUIRE(has_reason(insufficient.error(),
				TouchObservationErrorReason::QualifyingHoldNotMet));

	claim.qualifying_hold_threshold = 400ms;
	const auto retry = authority.value()->redeem_for_media_broker(claim);
	TEST_REQUIRE(!retry);
	TEST_REQUIRE(has_reason(retry.error(),
				TouchObservationErrorReason::ObservationInvalidated));
	return true;
}

bool expiry_at_the_exact_monotonic_deadline_fails_closed()
{
	FixtureMonotonicClock clock;
	FixtureObservationIdGenerator generator({"observation-id-fixture-00000001"});
	auto authority = create_fixture_authority(clock, generator);
	TEST_REQUIRE(authority.value()->record_committed_presentation(fixture_presentation()));
	const auto observation = record_fixture_contact(*authority.value(), clock, 500ms);
	clock.advance(1000ms);
	const auto expired = authority.value()->redeem_for_media_broker(fixture_claim(observation.value()));
	TEST_REQUIRE(!expired);
	TEST_REQUIRE(has_reason(expired.error(), TouchObservationErrorReason::ObservationExpired));
	return true;
}

bool explicit_lifecycle_events_invalidate_pending_observations()
{
	const TouchObservationInvalidation invalidations[]{
		TouchObservationInvalidation::UiDisconnected,
		TouchObservationInvalidation::Absence,
		TouchObservationInvalidation::Night,
		TouchObservationInvalidation::SuspendPreparation,
		TouchObservationInvalidation::SupervisorRestart,
	};
	std::uint64_t fixture_index = 1;
	for (const auto invalidation : invalidations) {
		FixtureMonotonicClock clock;
		FixtureObservationIdGenerator generator(
			{"observation-id-fixture-0000000" + std::to_string(fixture_index++)});
		auto authority = create_fixture_authority(clock, generator);
		TEST_REQUIRE(authority.value()->record_committed_presentation(
			fixture_presentation()));
		const auto observation = record_fixture_contact(*authority.value(), clock, 500ms);
		authority.value()->invalidate(invalidation);
		TEST_REQUIRE(authority.value()->available_observation_count() == 0);
		const auto rejected =
			authority.value()->redeem_for_media_broker(fixture_claim(observation.value()));
		TEST_REQUIRE(!rejected);
		TEST_REQUIRE(has_reason(rejected.error(),
					TouchObservationErrorReason::ObservationInvalidated));
		TEST_REQUIRE(rejected.error().context().at("invalidation_reason") ==
			     to_string(invalidation));
	}
	return true;
}

bool new_snapshot_and_render_generation_invalidate_prior_evidence()
{
	FixtureMonotonicClock snapshot_clock;
	FixtureObservationIdGenerator snapshot_generator(
		{"observation-id-fixture-00000001"});
	auto snapshot_authority = create_fixture_authority(snapshot_clock, snapshot_generator);
	auto presentation = fixture_presentation();
	TEST_REQUIRE(snapshot_authority.value()->record_committed_presentation(presentation));
	const auto snapshot_observation =
		record_fixture_contact(*snapshot_authority.value(), snapshot_clock, 500ms);
	++presentation.lifecycle_event_sequence;
	presentation.lifecycle_snapshot_id =
		"sha256:3333333333333333333333333333333333333333333333333333333333333333";
	TEST_REQUIRE(snapshot_authority.value()->record_committed_presentation(presentation));
	const auto stale_snapshot = snapshot_authority.value()->redeem_for_media_broker(
		fixture_claim(snapshot_observation.value()));
	TEST_REQUIRE(!stale_snapshot);
	TEST_REQUIRE(stale_snapshot.error().context().at("invalidation_reason") ==
		     "new_lifecycle_snapshot");

	FixtureMonotonicClock render_clock;
	FixtureObservationIdGenerator render_generator({"observation-id-fixture-00000002"});
	auto render_authority = create_fixture_authority(render_clock, render_generator);
	auto render_presentation = fixture_presentation();
	TEST_REQUIRE(render_authority.value()->record_committed_presentation(render_presentation));
	const auto render_observation =
		record_fixture_contact(*render_authority.value(), render_clock, 500ms);
	++render_presentation.committed_render_generation;
	TEST_REQUIRE(render_authority.value()->record_committed_presentation(
		render_presentation));
	const auto stale_render = render_authority.value()->redeem_for_media_broker(
		fixture_claim(render_observation.value()));
	TEST_REQUIRE(!stale_render);
	TEST_REQUIRE(stale_render.error().context().at("invalidation_reason") ==
		     "new_committed_render_generation");
	return true;
}

bool bounded_storage_rejects_live_overflow_and_reclaims_terminal_state()
{
	FixtureMonotonicClock clock;
	FixtureObservationIdGenerator generator({"observation-id-fixture-00000001",
						 "observation-id-fixture-00000002"});
	auto authority = create_fixture_authority(clock, generator, 1);
	TEST_REQUIRE(authority.value()->record_committed_presentation(fixture_presentation()));
	const auto first = record_fixture_contact(*authority.value(), clock, 500ms);
	const auto overflow = record_fixture_contact(*authority.value(), clock, 500ms);
	TEST_REQUIRE(!overflow);
	TEST_REQUIRE(has_reason(overflow.error(),
				TouchObservationErrorReason::ObservationCapacityExceeded));
	TEST_REQUIRE(generator.call_count() == 1);
	TEST_REQUIRE(authority.value()->redeem_for_media_broker(fixture_claim(first.value())));
	const auto replacement = record_fixture_contact(*authority.value(), clock, 500ms);
	TEST_REQUIRE(replacement);
	TEST_REQUIRE(replacement.value().touch_observation_id ==
		     "observation-id-fixture-00000002");
	TEST_REQUIRE(authority.value()->retained_observation_count() == 1);
	return true;
}

bool generator_failure_invalid_identity_and_collision_are_structured()
{
	FixtureMonotonicClock failure_clock;
	FixtureObservationIdGenerator failure_generator({"observation-id-fixture-00000001"});
	failure_generator.set_fail(true);
	auto failure_authority = create_fixture_authority(failure_clock, failure_generator);
	TEST_REQUIRE(failure_authority.value()->record_committed_presentation(
		fixture_presentation()));
	const auto generator_failure =
		record_fixture_contact(*failure_authority.value(), failure_clock, 500ms);
	TEST_REQUIRE(!generator_failure);
	TEST_REQUIRE(generator_failure.error().context().at("reason") ==
		     "observation_id_generation_failed");

	FixtureMonotonicClock invalid_clock;
	FixtureObservationIdGenerator invalid_generator({"short"});
	auto invalid_authority = create_fixture_authority(invalid_clock, invalid_generator);
	TEST_REQUIRE(invalid_authority.value()->record_committed_presentation(
		fixture_presentation()));
	const auto invalid_id =
		record_fixture_contact(*invalid_authority.value(), invalid_clock, 500ms);
	TEST_REQUIRE(!invalid_id);
	TEST_REQUIRE(has_reason(invalid_id.error(),
				TouchObservationErrorReason::ObservationIdInvalid));

	FixtureMonotonicClock collision_clock;
	FixtureObservationIdGenerator collision_generator(
		{"observation-id-fixture-00000001", "observation-id-fixture-00000001"});
	auto collision_authority = create_fixture_authority(collision_clock, collision_generator);
	TEST_REQUIRE(collision_authority.value()->record_committed_presentation(
		fixture_presentation()));
	TEST_REQUIRE(record_fixture_contact(*collision_authority.value(), collision_clock, 500ms));
	const auto collision =
		record_fixture_contact(*collision_authority.value(), collision_clock, 500ms);
	TEST_REQUIRE(!collision);
	TEST_REQUIRE(has_reason(collision.error(),
				TouchObservationErrorReason::ObservationIdCollision));
	return true;
}

bool monotonic_clock_regression_invalidates_live_evidence()
{
	FixtureMonotonicClock clock;
	FixtureObservationIdGenerator generator({"observation-id-fixture-00000001"});
	auto authority = create_fixture_authority(clock, generator);
	TEST_REQUIRE(authority.value()->record_committed_presentation(fixture_presentation()));
	const auto observation = record_fixture_contact(*authority.value(), clock, 500ms);
	clock.set(std::chrono::steady_clock::time_point{} + 9s);
	const auto regressed = authority.value()->redeem_for_media_broker(fixture_claim(observation.value()));
	TEST_REQUIRE(!regressed);
	TEST_REQUIRE(has_reason(regressed.error(),
				TouchObservationErrorReason::MonotonicClockRegressed));
	TEST_REQUIRE(authority.value()->available_observation_count() == 0);
	return true;
}

bool error_context_never_contains_observation_operation_or_coordinate_values()
{
	FixtureMonotonicClock clock;
	FixtureObservationIdGenerator generator({"observation-id-fixture-00000001"});
	auto authority = create_fixture_authority(clock, generator);
	TEST_REQUIRE(authority.value()->record_committed_presentation(fixture_presentation()));
	const auto observation = record_fixture_contact(*authority.value(), clock, 500ms);
	auto claim = fixture_claim(observation.value());
	claim.expected_presentation_binding.lifecycle_snapshot_id =
		"sha256:4444444444444444444444444444444444444444444444444444444444444444";
	const auto rejected = authority.value()->redeem_for_media_broker(claim);
	TEST_REQUIRE(!rejected);
	for (const auto &[key, value] : rejected.error().context()) {
		TEST_REQUIRE(key.find("coordinate") == std::string::npos);
		TEST_REQUIRE(value.find(observation.value().touch_observation_id) == std::string::npos);
		TEST_REQUIRE(value.find(claim.canonical_operation_digest) == std::string::npos);
	}
	return true;
}

} // namespace

int main()
{
	const struct {
		const char *name;
		bool (*run)();
	} tests[] = {
		{"configuration requires explicit bounded ttl and storage",
		 configuration_requires_explicit_bounded_ttl_and_storage},
		{"contact start requires current committed presentation",
		 contact_start_requires_current_committed_presentation},
		{"contact completion requires a start and duplicate start preserves time",
		 contact_completion_requires_a_start_and_duplicate_start_cannot_reset_time},
		{"contact duration is authority measured and bounded",
		 contact_duration_is_derived_from_authority_clock_and_bounded},
		{"protocol bindings and operation digest use exact grammars",
		 protocol_bindings_and_operation_digest_use_exact_grammars},
		{"generated observation binds complete supervisor evidence",
		 generated_observation_binds_complete_supervisor_evidence},
		{"successful redemption is one use and operation bound",
		 successful_redemption_is_one_use_and_operation_bound},
		{"personal context redemption is role bound outside the claim",
		 personal_context_redemption_is_role_bound_outside_the_claim},
		{"concurrent redemption has exactly one winner",
		 concurrent_redemption_has_exactly_one_winner},
		{"lifecycle invalidation bypasses slow identity generation",
		 lifecycle_invalidation_does_not_wait_for_slow_identity_generation},
		{"lifecycle invalidation cancels active contact",
		 lifecycle_invalidation_cancels_an_active_contact_before_completion},
		{"presentation mismatch terminally invalidates observation",
		 presentation_mismatch_terminally_invalidates_observation},
		{"insufficient qualifying hold terminally invalidates observation",
		 insufficient_qualifying_hold_terminally_invalidates_observation},
		{"expiry at exact deadline fails closed",
		 expiry_at_the_exact_monotonic_deadline_fails_closed},
		{"explicit lifecycle events invalidate pending observations",
		 explicit_lifecycle_events_invalidate_pending_observations},
		{"new snapshot and render invalidate prior evidence",
		 new_snapshot_and_render_generation_invalidate_prior_evidence},
		{"bounded storage rejects overflow and reclaims terminal state",
		 bounded_storage_rejects_live_overflow_and_reclaims_terminal_state},
		{"generator failures are structured",
		 generator_failure_invalid_identity_and_collision_are_structured},
		{"monotonic regression invalidates evidence",
		 monotonic_clock_regression_invalidates_live_evidence},
		{"error context excludes evidence values",
		 error_context_never_contains_observation_operation_or_coordinate_values},
	};

	for (const auto &test : tests) {
		if (!test.run()) {
			std::cerr << "FAILED: " << test.name << "\n";
			return 1;
		}
	}
	std::cout << "touch_observation_authority_test: "
		  << (sizeof(tests) / sizeof(tests[0])) << " passed\n";
	return 0;
}
