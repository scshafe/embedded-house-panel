#pragma once

#include "bhp/touch_observation_protocol/result.h"
#include "bhp/touch_observation_protocol/types.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace bhp::touch_observation_protocol {

#if defined(BHP_TOUCH_OBSERVATION_PROTOCOL_TEST_HOOKS)
struct BoundedNormalizedTouchObservationStreamTestAccess;
#endif

// @canonical normalized_touch_observation_stream -- supervisor-to-UI bounded try-only handoff.
class BoundedNormalizedTouchObservationStream final {
public:
	/**
	 * Allocates one explicitly bounded queue. Capacity must be 1 through
	 * kMaximumPendingObservationCount; this component supplies no runtime default.
	 */
	[[nodiscard]] static Result<std::unique_ptr<BoundedNormalizedTouchObservationStream>>
	create(BoundedStreamConfiguration configuration);

	/**
	 * Activates one committed presentation. A different binding requires a prior
	 * explicit invalidate call whose reason agrees with the transition.
	 */
	[[nodiscard]] Result<void>
	activate_presentation(LifecyclePresentationBinding presentation_binding);

	/**
	 * Makes all queued observations inaccessible immediately and never waits for
	 * the consumer. A contention failure means delivery is already closed while
	 * physical reclamation is deferred until activation. The supervisor producer
	 * owns this lifecycle operation.
	 */
	[[nodiscard]] Result<void> invalidate(InvalidationReason reason);

	/**
	 * Attempts one FIFO publication without waiting. Queue saturation or producer
	 * lock contention invalidates the stream so a partial contact cannot follow a
	 * dropped predecessor.
	 */
	[[nodiscard]] Result<void> try_publish(NormalizedTouchObservation observation);

	/**
	 * Attempts one FIFO receive without waiting. A successful empty optional means
	 * the active stream was observed empty; inactive states are typed failures.
	 */
	[[nodiscard]] Result<std::optional<NormalizedTouchObservation>> try_receive();

	[[nodiscard]] StreamState state() const noexcept;
	[[nodiscard]] InvalidationReason last_invalidation_reason() const noexcept;
	[[nodiscard]] std::size_t maximum_pending_observation_count() const noexcept;

private:
#if defined(BHP_TOUCH_OBSERVATION_PROTOCOL_TEST_HOOKS)
	friend struct BoundedNormalizedTouchObservationStreamTestAccess;
#endif
	explicit BoundedNormalizedTouchObservationStream(
		BoundedStreamConfiguration configuration);

	[[nodiscard]] Result<void> validate_presentation_successor(
		const LifecyclePresentationBinding &next_binding,
		std::uint64_t lifecycle_status) const;
	[[nodiscard]] Error inactive_error(std::string operation) const;
	[[nodiscard]] Error synchronization_error(std::string operation,
					       ErrorCode code) const;
	void transition_to_backpressure() noexcept;
	void clear_queue_locked() noexcept;

	BoundedStreamConfiguration configuration_;
	std::vector<std::optional<NormalizedTouchObservation>> queue_;
	std::size_t queue_head_{0};
	std::size_t queue_size_{0};
	std::optional<LifecyclePresentationBinding> current_presentation_binding_;
	std::uint64_t latest_observation_sequence_{0};
	// State, invalidation reason, and generation form one atomic lifecycle word so
	// activation can never overwrite a newer invalidation.
	std::atomic<std::uint64_t> lifecycle_status_{
		(static_cast<std::uint64_t>(InvalidationReason::SupervisorRestart) << 4U) |
		static_cast<std::uint64_t>(StreamState::AwaitingCommittedPresentation)};
	mutable std::mutex queue_mutex_;
#if defined(BHP_TOUCH_OBSERVATION_PROTOCOL_TEST_HOOKS)
	using ActivationCommitTestHook = void (*)(void *context);
	ActivationCommitTestHook activation_commit_test_hook_{nullptr};
	void *activation_commit_test_context_{nullptr};
#endif
};

} // namespace bhp::touch_observation_protocol
