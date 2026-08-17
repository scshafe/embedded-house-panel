#pragma once

#include "panel_supervisor/result.hpp"

#include "bhp/touch_observation_protocol/types.h"

#include <chrono>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace house_panel::supervisor {

inline constexpr std::uint32_t kTouchObservationProtocolVersion =
	bhp::touch_observation_protocol::kTouchObservationProtocolVersion;
inline constexpr std::chrono::milliseconds kMaximumTouchObservationTtl{5000};

enum class TouchObservationErrorReason {
	InvalidConfiguration,
	InvalidPresentationBinding,
	NoCommittedPresentation,
	ObservationIdGenerationFailed,
	ObservationIdInvalid,
	ObservationIdCollision,
	ObservationSequenceExhausted,
	ObservationCapacityExceeded,
	ContactAlreadyActive,
	ContactNotActive,
	ContactDurationInvalid,
	ObservationUnknown,
	ObservationExpired,
	ObservationAlreadyConsumed,
	ObservationInvalidated,
	PresentationMismatch,
	AudienceInvalid,
	OperationDigestInvalid,
	QualifyingHoldInvalid,
	QualifyingHoldNotMet,
	MonotonicClockReadFailed,
	MonotonicClockRegressed,
	MonotonicDeadlineOverflow,
};

enum class TouchObservationInvalidation {
	NewLifecycleSnapshot,
	NewCommittedRenderGeneration,
	UiDisconnected,
	Absence,
	Night,
	SuspendPreparation,
	SupervisorRestart,
	Expired,
	PresentationMismatch,
	QualifyingHoldNotMet,
	MonotonicClockFailure,
};

enum class TouchCapabilityHolderAudience { MediaBroker, PersonalContextClient };

[[nodiscard]] std::string_view to_string(TouchObservationErrorReason reason) noexcept;
[[nodiscard]] std::string_view to_string(TouchObservationInvalidation reason) noexcept;
[[nodiscard]] std::string_view to_string(TouchCapabilityHolderAudience audience) noexcept;

using TouchPresentationBinding =
	bhp::touch_observation_protocol::LifecyclePresentationBinding;

struct TouchObservationAuthorityConfiguration {
	std::chrono::milliseconds observation_ttl;
	std::chrono::milliseconds maximum_contact_duration;
	std::size_t maximum_retained_observations;
};

struct TouchObservationReceipt {
	std::uint32_t protocol_version;
	std::string touch_observation_id;
	std::uint64_t observation_sequence;
	TouchPresentationBinding presentation_binding;
	std::chrono::steady_clock::time_point contact_completed_at;
	std::chrono::milliseconds supervisor_measured_contact_duration;
	std::chrono::steady_clock::time_point expires_at;
};

struct TouchObservationClaim {
	std::string touch_observation_id;
	std::string canonical_operation_digest;
	TouchPresentationBinding expected_presentation_binding;
	std::optional<std::chrono::milliseconds> qualifying_hold_threshold;
};

struct TouchProvenanceEnvelope {
	std::uint32_t protocol_version;
	std::string touch_observation_id;
	std::uint64_t observation_sequence;
	TouchCapabilityHolderAudience capability_holder_audience;
	std::string canonical_operation_digest;
	TouchPresentationBinding presentation_binding;
	std::chrono::steady_clock::time_point contact_completed_at;
	std::chrono::milliseconds supervisor_measured_contact_duration;
	std::chrono::steady_clock::time_point expires_at;
	std::optional<std::chrono::milliseconds> qualifying_hold_threshold;
	bool qualifying_hold_satisfied;
};

using TouchObservationIdGenerator = std::function<Result<std::string>()>;
using TouchObservationMonotonicClock =
	std::function<std::chrono::steady_clock::time_point()>;

// @canonical touch_observation_authority -- supervisor-owned single-use physical-touch evidence.
class TouchObservationAuthority final {
public:
	/**
	 * Validates explicit TTL/storage/duration bounds and injected CSPRNG/clock ports.
	 * No production value is supplied by this class.
	 */
	[[nodiscard]] static Result<std::unique_ptr<TouchObservationAuthority>> create(
		TouchObservationAuthorityConfiguration configuration,
		TouchObservationIdGenerator observation_id_generator,
		TouchObservationMonotonicClock monotonic_clock);

	/**
	 * Installs the latest committed presentation and invalidates evidence bound to
	 * any prior lifecycle snapshot or render generation.
	 */
	[[nodiscard]] Result<void>
	record_committed_presentation(TouchPresentationBinding presentation_binding);

	/**
	 * Begins one contact on the currently committed presentation. The authority
	 * captures the monotonic start; callers cannot provide or reset the timestamp.
	 */
	[[nodiscard]] Result<void> record_contact_started();

	/**
	 * Completes the active contact and creates fresh evidence. The authority reads
	 * its monotonic clock again and derives duration from the retained start; no
	 * caller-supplied duration or qualification value is accepted.
	 */
	[[nodiscard]] Result<TouchObservationReceipt> record_contact_completed();

	/**
	 * Atomically consumes one observation for the media-broker capability and an
	 * opaque canonical operation digest. The future IPC adapter must select this
	 * entry point only after exact kernel peer authentication; audience is never a
	 * caller-supplied claim field.
	 */
	[[nodiscard]] Result<TouchProvenanceEnvelope>
	redeem_for_media_broker(const TouchObservationClaim &claim);

	/** Same role-bound operation for the personal-context capability holder. */
	[[nodiscard]] Result<TouchProvenanceEnvelope>
	redeem_for_personal_context_client(const TouchObservationClaim &claim);

	/** Invalidates every unconsumed observation for the named lifecycle event. */
	void invalidate(TouchObservationInvalidation reason) noexcept;

	[[nodiscard]] std::size_t retained_observation_count();
	[[nodiscard]] std::size_t available_observation_count();

private:
	enum class StoredState { Available, Consumed, Invalidated };

	struct StoredObservation {
		TouchObservationReceipt receipt;
		StoredState state;
		std::optional<TouchObservationInvalidation> invalidation_reason;
	};

	struct ActiveContactMeasurement {
		std::chrono::steady_clock::time_point started_at;
		TouchPresentationBinding presentation_binding;
	};

	TouchObservationAuthority(TouchObservationAuthorityConfiguration configuration,
				  TouchObservationIdGenerator observation_id_generator,
				  TouchObservationMonotonicClock monotonic_clock);

	[[nodiscard]] Result<std::chrono::steady_clock::time_point>
	read_monotonic_time(std::string_view operation);
	[[nodiscard]] Result<void> make_capacity_for_new_observation(
		std::chrono::steady_clock::time_point now);
	[[nodiscard]] Result<TouchProvenanceEnvelope> redeem_observation(
		const TouchObservationClaim &claim,
		TouchCapabilityHolderAudience capability_holder_audience);
	void invalidate_available(TouchObservationInvalidation reason) noexcept;
	void reconcile_external_invalidation() noexcept;

	TouchObservationAuthorityConfiguration configuration_;
	TouchObservationIdGenerator observation_id_generator_;
	TouchObservationMonotonicClock monotonic_clock_;
	std::optional<TouchPresentationBinding> current_presentation_binding_;
	std::optional<ActiveContactMeasurement> active_contact_measurement_;
	std::uint64_t current_invalidation_state_{0};
	std::optional<std::chrono::steady_clock::time_point> latest_monotonic_time_;
	std::uint64_t latest_observation_sequence_{0};
	std::map<std::string, StoredObservation> observations_;
	mutable std::mutex mutex_;
	std::atomic<std::uint64_t> external_invalidation_state_{0};
};

} // namespace house_panel::supervisor
