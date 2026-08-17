#include "bhp/touch_observation_protocol/bounded_normalized_touch_observation_stream.h"

#include "bhp/touch_observation_protocol/touch_observation_validation.h"

#include <exception>
#include <map>
#include <new>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace bhp::touch_observation_protocol {
namespace {

Error make_error(const ErrorCode code, std::string message, std::string operation,
		 std::map<std::string, std::string> context = {})
{
	context.emplace("operation", std::move(operation));
	return Error{code, std::move(message), std::move(context)};
}

bool is_lifecycle_snapshot_invalidation(const InvalidationReason reason) noexcept
{
	return reason == InvalidationReason::NewLifecycleSnapshot ||
	       reason == InvalidationReason::Absence || reason == InvalidationReason::Night ||
	       reason == InvalidationReason::SuspendPreparation;
}

constexpr std::uint64_t kLifecycleStateMask = 0x0fU;
constexpr std::uint64_t kLifecycleReasonMask = 0x0fU;
constexpr unsigned int kLifecycleReasonShift = 4U;
constexpr unsigned int kLifecycleEpochShift = 8U;

StreamState lifecycle_state(const std::uint64_t status) noexcept
{
	return static_cast<StreamState>(status & kLifecycleStateMask);
}

InvalidationReason lifecycle_reason(const std::uint64_t status) noexcept
{
	return static_cast<InvalidationReason>(
		(status >> kLifecycleReasonShift) & kLifecycleReasonMask);
}

std::uint64_t advanced_lifecycle_status(const std::uint64_t current,
					const StreamState state,
					const InvalidationReason reason) noexcept
{
	const auto next_epoch = (current >> kLifecycleEpochShift) + 1U;
	return (next_epoch << kLifecycleEpochShift) |
	       (static_cast<std::uint64_t>(reason) << kLifecycleReasonShift) |
	       static_cast<std::uint64_t>(state);
}

StreamState invalidated_state(const InvalidationReason reason) noexcept
{
	return reason == InvalidationReason::UiDisconnected
		       ? StreamState::Disconnected
		       : reason == InvalidationReason::StreamBackpressure
				 ? StreamState::Backpressured
				 : StreamState::AwaitingCommittedPresentation;
}

} // namespace

Result<std::unique_ptr<BoundedNormalizedTouchObservationStream>>
BoundedNormalizedTouchObservationStream::create(BoundedStreamConfiguration configuration)
{
	if (configuration.maximum_pending_observation_count == 0U ||
	    configuration.maximum_pending_observation_count >
		    kMaximumPendingObservationCount) {
		return Result<std::unique_ptr<BoundedNormalizedTouchObservationStream>>::failure(
			make_error(ErrorCode::ConfigurationInvalid,
				   "Touch observation stream requires an explicit bounded capacity",
				   "create",
				   {{"field", "maximum_pending_observation_count"},
				    {"bound", "1..1024"}}));
	}
	try {
		return Result<std::unique_ptr<BoundedNormalizedTouchObservationStream>>::success(
			std::unique_ptr<BoundedNormalizedTouchObservationStream>(
				new BoundedNormalizedTouchObservationStream(configuration)));
	} catch (const std::bad_alloc &) {
		return Result<std::unique_ptr<BoundedNormalizedTouchObservationStream>>::failure(
			make_error(ErrorCode::AllocationFailed,
				   "Failed to allocate the explicitly bounded touch observation stream",
				   "create"));
	} catch (const std::exception &) {
		return Result<std::unique_ptr<BoundedNormalizedTouchObservationStream>>::failure(
			make_error(ErrorCode::SynchronizationFailed,
				   "Failed to initialize touch observation stream synchronization",
				   "create"));
	} catch (...) {
		return Result<std::unique_ptr<BoundedNormalizedTouchObservationStream>>::failure(
			make_error(ErrorCode::SynchronizationFailed,
				   "Failed to initialize touch observation stream synchronization",
				   "create"));
	}
}

BoundedNormalizedTouchObservationStream::BoundedNormalizedTouchObservationStream(
	BoundedStreamConfiguration configuration)
	: configuration_(configuration),
	  queue_(configuration.maximum_pending_observation_count)
{
}

Result<void> BoundedNormalizedTouchObservationStream::validate_presentation_successor(
	const LifecyclePresentationBinding &next_binding,
	const std::uint64_t lifecycle_status) const
{
	if (!current_presentation_binding_.has_value()) {
		return Result<void>::success();
	}
	const auto &previous = *current_presentation_binding_;
	const auto current_state = lifecycle_state(lifecycle_status);
	const auto reason = lifecycle_reason(lifecycle_status);
	if (lifecycle_presentation_bindings_equal(previous, next_binding)) {
		if (current_state == StreamState::Active) {
			return Result<void>::success();
		}
		return Result<void>::failure(make_error(
			ErrorCode::LifecycleBindingStale,
			"Rejected reactivation of a presentation invalidated by lifecycle state",
			"activate_presentation",
			{{"reason", std::string(to_string(reason))}}));
	}
	if (current_state == StreamState::Active) {
		return Result<void>::failure(make_error(
			ErrorCode::LifecycleTransitionInvalid,
			"A changed presentation requires explicit lifecycle invalidation",
			"activate_presentation"));
	}

	bool valid_successor = false;
	if (reason == InvalidationReason::SupervisorRestart) {
		valid_successor = next_binding.supervisor_session_id !=
				  previous.supervisor_session_id;
	} else if (reason == InvalidationReason::UiDisconnected ||
		   reason == InvalidationReason::StreamBackpressure) {
		valid_successor =
			next_binding.supervisor_session_id == previous.supervisor_session_id &&
			next_binding.ui_connection_challenge != previous.ui_connection_challenge &&
			next_binding.lifecycle_event_sequence >=
				previous.lifecycle_event_sequence &&
			(next_binding.lifecycle_event_sequence !=
				 previous.lifecycle_event_sequence ||
			 next_binding.lifecycle_snapshot_id == previous.lifecycle_snapshot_id);
	} else if (reason == InvalidationReason::NewCommittedRenderGeneration) {
		valid_successor =
			next_binding.supervisor_session_id == previous.supervisor_session_id &&
			next_binding.ui_connection_challenge == previous.ui_connection_challenge &&
			next_binding.lifecycle_event_sequence == previous.lifecycle_event_sequence &&
			next_binding.lifecycle_snapshot_id == previous.lifecycle_snapshot_id &&
			next_binding.committed_render_generation >
				previous.committed_render_generation;
	} else if (is_lifecycle_snapshot_invalidation(reason)) {
		valid_successor =
			next_binding.supervisor_session_id == previous.supervisor_session_id &&
			next_binding.lifecycle_event_sequence > previous.lifecycle_event_sequence;
	}
	if (!valid_successor) {
		return Result<void>::failure(make_error(
			ErrorCode::LifecycleBindingStale,
			"Rejected a stale or reason-inconsistent presentation successor",
			"activate_presentation", {{"reason", std::string(to_string(reason))}}));
	}
	return Result<void>::success();
}

Result<void> BoundedNormalizedTouchObservationStream::activate_presentation(
	LifecyclePresentationBinding presentation_binding)
{
	auto binding_result = validate_lifecycle_presentation_binding(presentation_binding);
	if (!binding_result) {
		return binding_result;
	}
	const auto entry_status = lifecycle_status_.load(std::memory_order_acquire);
	try {
		std::unique_lock<std::mutex> lock(queue_mutex_, std::try_to_lock);
		if (!lock.owns_lock()) {
			return Result<void>::failure(synchronization_error(
				"activate_presentation", ErrorCode::SynchronizationContended));
		}
		if (entry_status != lifecycle_status_.load(std::memory_order_acquire)) {
			return Result<void>::failure(inactive_error("activate_presentation"));
		}
		auto successor_result =
			validate_presentation_successor(presentation_binding, entry_status);
		if (!successor_result) {
			return successor_result;
		}
		if (current_presentation_binding_.has_value() &&
		    lifecycle_presentation_bindings_equal(*current_presentation_binding_,
						  presentation_binding) &&
		    lifecycle_state(entry_status) == StreamState::Active) {
			return Result<void>::success();
		}
#if defined(BHP_TOUCH_OBSERVATION_PROTOCOL_TEST_HOOKS)
		if (activation_commit_test_hook_ != nullptr) {
			activation_commit_test_hook_(activation_commit_test_context_);
		}
#endif
		const auto active_status = advanced_lifecycle_status(
			entry_status, StreamState::Active, lifecycle_reason(entry_status));
		auto expected_status = entry_status;
		if (!lifecycle_status_.compare_exchange_strong(
			    expected_status, active_status, std::memory_order_acq_rel,
			    std::memory_order_acquire)) {
			return Result<void>::failure(inactive_error("activate_presentation"));
		}
		const bool supervisor_session_changed =
			!current_presentation_binding_.has_value() ||
			current_presentation_binding_->supervisor_session_id !=
				presentation_binding.supervisor_session_id;
		clear_queue_locked();
		if (supervisor_session_changed) {
			latest_observation_sequence_ = 0U;
		}
		current_presentation_binding_ = std::move(presentation_binding);
		if (lifecycle_status_.load(std::memory_order_acquire) != active_status) {
			clear_queue_locked();
			return Result<void>::failure(inactive_error("activate_presentation"));
		}
		return Result<void>::success();
	} catch (const std::system_error &) {
		return Result<void>::failure(synchronization_error(
			"activate_presentation", ErrorCode::SynchronizationFailed));
	} catch (...) {
		return Result<void>::failure(synchronization_error(
			"activate_presentation", ErrorCode::SynchronizationFailed));
	}
}

Result<void> BoundedNormalizedTouchObservationStream::invalidate(
	const InvalidationReason reason)
{
	auto current_status = lifecycle_status_.load(std::memory_order_acquire);
	while (!lifecycle_status_.compare_exchange_weak(
		current_status,
		advanced_lifecycle_status(current_status, invalidated_state(reason), reason),
		std::memory_order_acq_rel, std::memory_order_acquire)) {
	}
	try {
		std::unique_lock<std::mutex> lock(queue_mutex_, std::try_to_lock);
		if (!lock.owns_lock()) {
			return Result<void>::failure(synchronization_error(
				"invalidate", ErrorCode::SynchronizationContended));
		}
		clear_queue_locked();
		return Result<void>::success();
	} catch (const std::system_error &) {
		return Result<void>::failure(
			synchronization_error("invalidate", ErrorCode::SynchronizationFailed));
	} catch (...) {
		return Result<void>::failure(
			synchronization_error("invalidate", ErrorCode::SynchronizationFailed));
	}
}

Result<void> BoundedNormalizedTouchObservationStream::try_publish(
	NormalizedTouchObservation observation)
{
	auto observation_result = validate_normalized_touch_observation(observation);
	if (!observation_result) {
		return observation_result;
	}
	const auto entry_status = lifecycle_status_.load(std::memory_order_acquire);
	if (lifecycle_state(entry_status) != StreamState::Active) {
		return Result<void>::failure(inactive_error("try_publish"));
	}
	try {
		std::unique_lock<std::mutex> lock(queue_mutex_, std::try_to_lock);
		if (!lock.owns_lock()) {
			transition_to_backpressure();
			return Result<void>::failure(synchronization_error(
				"try_publish", ErrorCode::SynchronizationContended));
		}
		if (lifecycle_status_.load(std::memory_order_acquire) != entry_status) {
			return Result<void>::failure(inactive_error("try_publish"));
		}
		if (!current_presentation_binding_.has_value() ||
		    !lifecycle_presentation_bindings_equal(
			    *current_presentation_binding_, observation.presentation_binding)) {
			return Result<void>::failure(make_error(
				ErrorCode::LifecycleBindingStale,
				"Touch observation does not match the active committed presentation",
				"try_publish", {{"field", "presentation_binding"}}));
		}
		if (observation.observation_sequence <= latest_observation_sequence_) {
			return Result<void>::failure(make_error(
				ErrorCode::ObservationSequenceNonmonotonic,
				"Touch observation sequence is not strictly increasing",
				"try_publish", {{"field", "observation_sequence"}}));
		}
		if (queue_size_ == queue_.size()) {
			clear_queue_locked();
			transition_to_backpressure();
			return Result<void>::failure(make_error(
				ErrorCode::QueueFull,
				"Touch observation queue is full; the stream was invalidated",
				"try_publish",
				{{"bound", "maximum_pending_observation_count"}}));
		}
		const auto queue_tail = (queue_head_ + queue_size_) % queue_.size();
		queue_[queue_tail].emplace(std::move(observation));
		latest_observation_sequence_ = queue_[queue_tail]->observation_sequence;
		++queue_size_;
		if (lifecycle_status_.load(std::memory_order_acquire) != entry_status) {
			clear_queue_locked();
			return Result<void>::failure(inactive_error("try_publish"));
		}
		return Result<void>::success();
	} catch (const std::system_error &) {
		transition_to_backpressure();
		return Result<void>::failure(
			synchronization_error("try_publish", ErrorCode::SynchronizationFailed));
	} catch (...) {
		transition_to_backpressure();
		return Result<void>::failure(
			synchronization_error("try_publish", ErrorCode::SynchronizationFailed));
	}
}

Result<std::optional<NormalizedTouchObservation>>
BoundedNormalizedTouchObservationStream::try_receive()
{
	const auto entry_status = lifecycle_status_.load(std::memory_order_acquire);
	if (lifecycle_state(entry_status) != StreamState::Active) {
		return Result<std::optional<NormalizedTouchObservation>>::failure(
			inactive_error("try_receive"));
	}
	try {
		std::unique_lock<std::mutex> lock(queue_mutex_, std::try_to_lock);
		if (!lock.owns_lock()) {
			return Result<std::optional<NormalizedTouchObservation>>::failure(
				synchronization_error("try_receive",
						      ErrorCode::SynchronizationContended));
		}
		if (lifecycle_status_.load(std::memory_order_acquire) != entry_status) {
			return Result<std::optional<NormalizedTouchObservation>>::failure(
				inactive_error("try_receive"));
		}
		if (queue_size_ == 0U) {
			return Result<std::optional<NormalizedTouchObservation>>::success(
				std::nullopt);
		}
		NormalizedTouchObservation observation = std::move(*queue_[queue_head_]);
		queue_[queue_head_].reset();
		queue_head_ = (queue_head_ + 1U) % queue_.size();
		--queue_size_;
		if (lifecycle_status_.load(std::memory_order_acquire) != entry_status) {
			return Result<std::optional<NormalizedTouchObservation>>::failure(
				inactive_error("try_receive"));
		}
		return Result<std::optional<NormalizedTouchObservation>>::success(
			std::move(observation));
	} catch (const std::system_error &) {
		return Result<std::optional<NormalizedTouchObservation>>::failure(
			synchronization_error("try_receive", ErrorCode::SynchronizationFailed));
	} catch (...) {
		return Result<std::optional<NormalizedTouchObservation>>::failure(
			synchronization_error("try_receive", ErrorCode::SynchronizationFailed));
	}
}

StreamState BoundedNormalizedTouchObservationStream::state() const noexcept
{
	return lifecycle_state(lifecycle_status_.load(std::memory_order_acquire));
}

InvalidationReason
BoundedNormalizedTouchObservationStream::last_invalidation_reason() const noexcept
{
	return lifecycle_reason(lifecycle_status_.load(std::memory_order_acquire));
}

std::size_t
BoundedNormalizedTouchObservationStream::maximum_pending_observation_count() const noexcept
{
	return configuration_.maximum_pending_observation_count;
}

Error BoundedNormalizedTouchObservationStream::inactive_error(std::string operation) const
{
	const auto status = lifecycle_status_.load(std::memory_order_acquire);
	const auto current_state = lifecycle_state(status);
	const auto reason = lifecycle_reason(status);
	if (current_state == StreamState::Disconnected) {
		return make_error(ErrorCode::UiDisconnected,
				  "Touch observation stream is disconnected", std::move(operation),
				  {{"reason", std::string(to_string(reason))}});
	}
	if (current_state == StreamState::Backpressured) {
		return make_error(ErrorCode::StreamBackpressured,
				  "Touch observation stream is invalidated by backpressure",
				  std::move(operation),
				  {{"reason", std::string(to_string(reason))}});
	}
	return make_error(ErrorCode::LifecycleInactive,
			  "Touch observation stream has no active committed presentation",
			  std::move(operation),
			  {{"reason", std::string(to_string(reason))}});
}

Error BoundedNormalizedTouchObservationStream::synchronization_error(
	std::string operation, const ErrorCode code) const
{
	return make_error(code,
			  code == ErrorCode::SynchronizationContended
				  ? "Touch observation stream synchronization is contended"
				  : "Touch observation stream synchronization failed",
			  std::move(operation));
}

void BoundedNormalizedTouchObservationStream::transition_to_backpressure() noexcept
{
	auto current_status = lifecycle_status_.load(std::memory_order_acquire);
	while (!lifecycle_status_.compare_exchange_weak(
		current_status,
		advanced_lifecycle_status(current_status, StreamState::Backpressured,
					  InvalidationReason::StreamBackpressure),
		std::memory_order_acq_rel, std::memory_order_acquire)) {
	}
}

void BoundedNormalizedTouchObservationStream::clear_queue_locked() noexcept
{
	for (auto &observation : queue_) {
		observation.reset();
	}
	queue_head_ = 0U;
	queue_size_ = 0U;
}

} // namespace bhp::touch_observation_protocol
