#include "bhp/touch_observation_protocol/bounded_normalized_touch_observation_stream.h"
#include "bhp/touch_observation_protocol/touch_observation_validation.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace bhp::touch_observation_protocol {

#if defined(BHP_TOUCH_OBSERVATION_PROTOCOL_TEST_HOOKS)
struct BoundedNormalizedTouchObservationStreamTestAccess {
	static void set_activation_commit_hook(
		BoundedNormalizedTouchObservationStream &stream, void (*hook)(void *),
		void *context) noexcept
	{
		stream.activation_commit_test_hook_ = hook;
		stream.activation_commit_test_context_ = context;
	}
};
#endif

namespace {

using namespace std::chrono_literals;

#define TEST_REQUIRE(condition)                                                                    \
	do {                                                                                       \
		if (!(condition)) {                                                                \
			std::cerr << __func__ << ": requirement failed: " #condition << '\n';      \
			return false;                                                              \
		}                                                                                  \
	} while (false)

LifecyclePresentationBinding fixture_presentation()
{
	return LifecyclePresentationBinding{
		2U,
		"00112233445566778899aabbccddeeff",
		40U,
		"sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
		"ffeeddccbbaa99887766554433221100",
		7U,
	};
}

NormalizedTouchObservation fixture_observation(
	const std::uint64_t sequence, const ContactState state,
	const std::uint64_t duration_milliseconds,
	LifecyclePresentationBinding presentation_binding = fixture_presentation())
{
	return NormalizedTouchObservation{
		kTouchObservationProtocolVersion,
		"touch-observation-000001",
		sequence,
		std::move(presentation_binding),
		-125,
		4096,
		state,
		duration_milliseconds,
	};
}

std::unique_ptr<BoundedNormalizedTouchObservationStream>
fixture_active_stream(const std::size_t capacity)
{
	auto created = BoundedNormalizedTouchObservationStream::create(
		BoundedStreamConfiguration{capacity});
	if (!created) {
		return nullptr;
	}
	auto stream = std::move(created).value();
	if (!stream->activate_presentation(fixture_presentation())) {
		return nullptr;
	}
	return stream;
}

bool error_context_excludes_sensitive_values(const Error &error)
{
	const std::vector<std::string_view> forbidden_values{
		"00112233445566778899aabbccddeeff",
		"ffeeddccbbaa99887766554433221100",
		"sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
		"touch-observation-000001",
		"-125",
		"4096",
	};
	for (const auto &[key, value] : error.context) {
		(void)key;
		for (const auto forbidden : forbidden_values) {
			if (value.find(forbidden) != std::string::npos) {
				return false;
			}
		}
	}
	return true;
}

bool configuration_requires_an_explicit_bounded_capacity()
{
	const auto zero = BoundedNormalizedTouchObservationStream::create(
		BoundedStreamConfiguration{0U});
	TEST_REQUIRE(!zero);
	TEST_REQUIRE(zero.error().code == ErrorCode::ConfigurationInvalid);

	const auto excessive = BoundedNormalizedTouchObservationStream::create(
		BoundedStreamConfiguration{kMaximumPendingObservationCount + 1U});
	TEST_REQUIRE(!excessive);
	TEST_REQUIRE(excessive.error().code == ErrorCode::ConfigurationInvalid);

	auto valid = BoundedNormalizedTouchObservationStream::create(
		BoundedStreamConfiguration{1U});
	TEST_REQUIRE(valid);
	TEST_REQUIRE(valid.value()->maximum_pending_observation_count() == 1U);
	TEST_REQUIRE(valid.value()->state() ==
		     StreamState::AwaitingCommittedPresentation);
	return true;
}

bool validation_is_strict_and_errors_do_not_echo_evidence()
{
	auto invalid_protocol = fixture_observation(1U, ContactState::Started, 0U);
	invalid_protocol.touch_observation_protocol_version = 2U;
	const auto protocol_result =
		validate_normalized_touch_observation(invalid_protocol);
	TEST_REQUIRE(!protocol_result);
	TEST_REQUIRE(protocol_result.error().code == ErrorCode::TouchProtocolUnsupported);

	auto invalid_id = fixture_observation(1U, ContactState::Started, 0U);
	invalid_id.touch_observation_id = "contains a space and secret";
	const auto id_result = validate_normalized_touch_observation(invalid_id);
	TEST_REQUIRE(!id_result);
	TEST_REQUIRE(id_result.error().code == ErrorCode::ObservationIdInvalid);
	TEST_REQUIRE(id_result.error().context.at("field") == "touch_observation_id");

	auto invalid_binding = fixture_observation(1U, ContactState::Started, 0U);
	invalid_binding.presentation_binding.ui_connection_challenge =
		"00000000000000000000000000000000";
	const auto binding_result =
		validate_normalized_touch_observation(invalid_binding);
	TEST_REQUIRE(!binding_result);
	TEST_REQUIRE(binding_result.error().code == ErrorCode::LifecycleBindingInvalid);
	TEST_REQUIRE(error_context_excludes_sensitive_values(binding_result.error()));

	auto invalid_axis = fixture_observation(1U, ContactState::Started, 0U);
	invalid_axis.raw_axis_x = std::numeric_limits<std::int64_t>::min();
	const auto axis_result = validate_normalized_touch_observation(invalid_axis);
	TEST_REQUIRE(!axis_result);
	TEST_REQUIRE(axis_result.error().code == ErrorCode::RawAxisInvalid);
	TEST_REQUIRE(error_context_excludes_sensitive_values(axis_result.error()));

	auto invalid_state = fixture_observation(1U, ContactState::Started, 0U);
	invalid_state.contact_state = static_cast<ContactState>(0xffU);
	const auto state_result = validate_normalized_touch_observation(invalid_state);
	TEST_REQUIRE(!state_result);
	TEST_REQUIRE(state_result.error().code == ErrorCode::ContactStateInvalid);

	auto premature_duration = fixture_observation(1U, ContactState::Updated, 1U);
	const auto premature_result =
		validate_normalized_touch_observation(premature_duration);
	TEST_REQUIRE(!premature_result);
	TEST_REQUIRE(premature_result.error().code == ErrorCode::ContactDurationInvalid);

	auto missing_duration = fixture_observation(1U, ContactState::Completed, 0U);
	const auto missing_result = validate_normalized_touch_observation(missing_duration);
	TEST_REQUIRE(!missing_result);
	TEST_REQUIRE(missing_result.error().code == ErrorCode::ContactDurationInvalid);
	return true;
}

bool active_stream_preserves_fifo_order_and_reports_empty_distinctly()
{
	auto stream = fixture_active_stream(3U);
	TEST_REQUIRE(stream != nullptr);
	TEST_REQUIRE(stream->try_publish(fixture_observation(1U, ContactState::Started, 0U)));
	TEST_REQUIRE(stream->try_publish(fixture_observation(2U, ContactState::Updated, 0U)));
	TEST_REQUIRE(stream->try_publish(fixture_observation(3U, ContactState::Completed, 25U)));

	for (std::uint64_t expected_sequence = 1U; expected_sequence <= 3U;
	     ++expected_sequence) {
		auto received = stream->try_receive();
		TEST_REQUIRE(received);
		TEST_REQUIRE(received.value().has_value());
		TEST_REQUIRE(received.value()->observation_sequence == expected_sequence);
	}
	auto empty = stream->try_receive();
	TEST_REQUIRE(empty);
	TEST_REQUIRE(!empty.value().has_value());
	return true;
}

bool full_queue_rejects_immediately_and_invalidates_partial_contact()
{
	auto stream = fixture_active_stream(2U);
	TEST_REQUIRE(stream != nullptr);
	TEST_REQUIRE(stream->try_publish(fixture_observation(1U, ContactState::Started, 0U)));
	TEST_REQUIRE(stream->try_publish(fixture_observation(2U, ContactState::Updated, 0U)));

	const auto started_at = std::chrono::steady_clock::now();
	const auto full =
		stream->try_publish(fixture_observation(3U, ContactState::Completed, 40U));
	const auto elapsed = std::chrono::steady_clock::now() - started_at;
	TEST_REQUIRE(!full);
	TEST_REQUIRE(full.error().code == ErrorCode::QueueFull);
	TEST_REQUIRE(elapsed < 100ms);
	TEST_REQUIRE(stream->state() == StreamState::Backpressured);
	TEST_REQUIRE(stream->last_invalidation_reason() ==
		     InvalidationReason::StreamBackpressure);
	TEST_REQUIRE(error_context_excludes_sensitive_values(full.error()));

	const auto inaccessible = stream->try_receive();
	TEST_REQUIRE(!inaccessible);
	TEST_REQUIRE(inaccessible.error().code == ErrorCode::StreamBackpressured);
	return true;
}

bool nonmonotonic_and_stale_observations_are_rejected()
{
	auto stream = fixture_active_stream(3U);
	TEST_REQUIRE(stream != nullptr);
	TEST_REQUIRE(stream->try_publish(fixture_observation(5U, ContactState::Started, 0U)));
	const auto duplicate =
		stream->try_publish(fixture_observation(5U, ContactState::Updated, 0U));
	TEST_REQUIRE(!duplicate);
	TEST_REQUIRE(duplicate.error().code ==
		     ErrorCode::ObservationSequenceNonmonotonic);

	auto stale_binding = fixture_presentation();
	stale_binding.committed_render_generation = 6U;
	const auto stale = stream->try_publish(
		fixture_observation(6U, ContactState::Updated, 0U, stale_binding));
	TEST_REQUIRE(!stale);
	TEST_REQUIRE(stale.error().code == ErrorCode::LifecycleBindingStale);
	TEST_REQUIRE(error_context_excludes_sensitive_values(stale.error()));
	return true;
}

bool lifecycle_invalidation_clears_stale_records_and_enforces_successors()
{
	auto stream = fixture_active_stream(2U);
	TEST_REQUIRE(stream != nullptr);
	TEST_REQUIRE(stream->try_publish(fixture_observation(1U, ContactState::Started, 0U)));
	TEST_REQUIRE(stream->invalidate(InvalidationReason::NewLifecycleSnapshot));

	const auto inactive = stream->try_receive();
	TEST_REQUIRE(!inactive);
	TEST_REQUIRE(inactive.error().code == ErrorCode::LifecycleInactive);

	const auto stale_activation = stream->activate_presentation(fixture_presentation());
	TEST_REQUIRE(!stale_activation);
	TEST_REQUIRE(stale_activation.error().code == ErrorCode::LifecycleBindingStale);

	auto next = fixture_presentation();
	next.lifecycle_event_sequence = 41U;
	next.lifecycle_snapshot_id =
		"sha256:abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789";
	next.committed_render_generation = 8U;
	TEST_REQUIRE(stream->activate_presentation(next));
	auto empty = stream->try_receive();
	TEST_REQUIRE(empty);
	TEST_REQUIRE(!empty.value().has_value());

	TEST_REQUIRE(stream->invalidate(InvalidationReason::UiDisconnected));
	const auto disconnected = stream->try_receive();
	TEST_REQUIRE(!disconnected);
	TEST_REQUIRE(disconnected.error().code == ErrorCode::UiDisconnected);
	const auto replayed_connection = stream->activate_presentation(next);
	TEST_REQUIRE(!replayed_connection);

	next.ui_connection_challenge = "0123456789abcdef0123456789abcdef";
	TEST_REQUIRE(stream->activate_presentation(next));
	TEST_REQUIRE(stream->state() == StreamState::Active);
	return true;
}

bool changed_binding_requires_explicit_reason_and_render_successor_is_exact()
{
	auto stream = fixture_active_stream(2U);
	TEST_REQUIRE(stream != nullptr);
	auto next_render = fixture_presentation();
	next_render.committed_render_generation = 8U;
	const auto implicit_change = stream->activate_presentation(next_render);
	TEST_REQUIRE(!implicit_change);
	TEST_REQUIRE(implicit_change.error().code ==
		     ErrorCode::LifecycleTransitionInvalid);

	TEST_REQUIRE(
		stream->invalidate(InvalidationReason::NewCommittedRenderGeneration));
	auto wrong_snapshot = next_render;
	wrong_snapshot.lifecycle_snapshot_id =
		"sha256:abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789";
	const auto mismatched = stream->activate_presentation(wrong_snapshot);
	TEST_REQUIRE(!mismatched);
	TEST_REQUIRE(mismatched.error().code == ErrorCode::LifecycleBindingStale);
	TEST_REQUIRE(stream->activate_presentation(next_render));
	return true;
}

bool supervisor_restart_requires_a_new_session_and_resets_observation_sequence()
{
	auto stream = fixture_active_stream(2U);
	TEST_REQUIRE(stream != nullptr);
	TEST_REQUIRE(stream->try_publish(fixture_observation(50U, ContactState::Started, 0U)));
	TEST_REQUIRE(stream->invalidate(InvalidationReason::SupervisorRestart));

	const auto same_session = stream->activate_presentation(fixture_presentation());
	TEST_REQUIRE(!same_session);
	TEST_REQUIRE(same_session.error().code == ErrorCode::LifecycleBindingStale);

	auto restarted = fixture_presentation();
	restarted.supervisor_session_id = "102132435465768798a9bacbdcedfe0f";
	restarted.ui_connection_challenge = "0ffedccbbaa998877665544332211020";
	restarted.lifecycle_event_sequence = 1U;
	restarted.committed_render_generation = 1U;
	TEST_REQUIRE(stream->activate_presentation(restarted));
	TEST_REQUIRE(stream->try_publish(
		fixture_observation(1U, ContactState::Started, 0U, restarted)));
	auto received = stream->try_receive();
	TEST_REQUIRE(received && received.value().has_value());
	TEST_REQUIRE(received.value()->observation_sequence == 1U);
	return true;
}

#if defined(BHP_TOUCH_OBSERVATION_PROTOCOL_TEST_HOOKS)
struct ActivationBarrier {
	std::mutex mutex;
	std::condition_variable condition;
	bool activation_reached_commit{false};
	bool release_activation{false};
};

void wait_before_activation_commit(void *context)
{
	auto &barrier = *static_cast<ActivationBarrier *>(context);
	std::unique_lock<std::mutex> lock(barrier.mutex);
	barrier.activation_reached_commit = true;
	barrier.condition.notify_all();
	barrier.condition.wait(lock, [&barrier]() { return barrier.release_activation; });
}

bool concurrent_invalidation_cannot_be_overwritten_by_activation()
{
	auto stream = fixture_active_stream(2U);
	TEST_REQUIRE(stream != nullptr);
	TEST_REQUIRE(stream->invalidate(InvalidationReason::NewLifecycleSnapshot));

	auto next = fixture_presentation();
	next.lifecycle_event_sequence = 41U;
	next.lifecycle_snapshot_id =
		"sha256:abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789";
	next.committed_render_generation = 8U;

	ActivationBarrier barrier;
	BoundedNormalizedTouchObservationStreamTestAccess::set_activation_commit_hook(
		*stream, wait_before_activation_commit, &barrier);
	std::optional<Result<void>> activation_result;
	std::thread activation([&]() {
		activation_result.emplace(stream->activate_presentation(next));
	});
	{
		std::unique_lock<std::mutex> lock(barrier.mutex);
		barrier.condition.wait(lock, [&barrier]() {
			return barrier.activation_reached_commit;
		});
	}

	const auto invalidation = stream->invalidate(InvalidationReason::Absence);
	const auto state_after_invalidation = stream->state();
	const auto reason_after_invalidation = stream->last_invalidation_reason();
	{
		std::lock_guard<std::mutex> lock(barrier.mutex);
		barrier.release_activation = true;
	}
	barrier.condition.notify_all();
	activation.join();

	TEST_REQUIRE(!invalidation);
	TEST_REQUIRE(invalidation.error().code == ErrorCode::SynchronizationContended);
	TEST_REQUIRE(state_after_invalidation ==
		     StreamState::AwaitingCommittedPresentation);
	TEST_REQUIRE(reason_after_invalidation == InvalidationReason::Absence);
	TEST_REQUIRE(activation_result.has_value());
	TEST_REQUIRE(!activation_result.value());
	TEST_REQUIRE(stream->state() == StreamState::AwaitingCommittedPresentation);
	TEST_REQUIRE(stream->last_invalidation_reason() == InvalidationReason::Absence);
	return true;
}
#endif

} // namespace
} // namespace bhp::touch_observation_protocol

int main()
{
	using Test = bool (*)();
	const std::vector<Test> tests{
		bhp::touch_observation_protocol::configuration_requires_an_explicit_bounded_capacity,
		bhp::touch_observation_protocol::validation_is_strict_and_errors_do_not_echo_evidence,
		bhp::touch_observation_protocol::active_stream_preserves_fifo_order_and_reports_empty_distinctly,
		bhp::touch_observation_protocol::full_queue_rejects_immediately_and_invalidates_partial_contact,
		bhp::touch_observation_protocol::nonmonotonic_and_stale_observations_are_rejected,
		bhp::touch_observation_protocol::lifecycle_invalidation_clears_stale_records_and_enforces_successors,
		bhp::touch_observation_protocol::changed_binding_requires_explicit_reason_and_render_successor_is_exact,
		bhp::touch_observation_protocol::supervisor_restart_requires_a_new_session_and_resets_observation_sequence,
#if defined(BHP_TOUCH_OBSERVATION_PROTOCOL_TEST_HOOKS)
		bhp::touch_observation_protocol::concurrent_invalidation_cannot_be_overwritten_by_activation,
#endif
	};
	std::size_t passed = 0U;
	for (const auto test : tests) {
		if (!test()) {
			return 1;
		}
		++passed;
	}
	std::cout << "touch_observation_protocol_test: " << passed << " passed\n";
	return 0;
}
