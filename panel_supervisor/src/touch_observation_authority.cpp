#include "panel_supervisor/touch_observation_authority.hpp"

#include "bhp/touch_observation_protocol/touch_observation_validation.h"

#include <algorithm>
#include <exception>
#include <limits>
#include <utility>

namespace house_panel::supervisor {
namespace {

constexpr std::size_t kMinimumObservationIdBytes = 22;
constexpr std::size_t kMaximumObservationIdBytes = 128;
constexpr std::size_t kSha256IdentifierBytes = 71;
constexpr std::uint64_t kInvalidationReasonMask = 0xFF;
static_assert(static_cast<std::uint64_t>(
		      TouchObservationInvalidation::MonotonicClockFailure) <=
	      kInvalidationReasonMask);

SupervisorError make_error(const TouchObservationErrorReason reason,
			   const std::string_view operation, std::string message,
			   const ErrorCode error_code = ErrorCode::InvalidPolicyObservation,
			   std::map<std::string, std::string> extra_context = {})
{
	extra_context.emplace("operation", std::string(operation));
	extra_context.emplace("reason", std::string(to_string(reason)));
	return SupervisorError(error_code, std::move(message), std::move(extra_context));
}

bool is_safe_opaque_text(const std::string_view value, const std::size_t minimum_bytes,
			 const std::size_t maximum_bytes) noexcept
{
	if (value.size() < minimum_bytes || value.size() > maximum_bytes) {
		return false;
	}
	return std::all_of(value.begin(), value.end(), [](const char character) {
		const bool ascii_alphanumeric =
			(character >= 'A' && character <= 'Z') ||
			(character >= 'a' && character <= 'z') ||
			(character >= '0' && character <= '9');
		return ascii_alphanumeric || character == '-' || character == '_' ||
		       character == '.' || character == ':';
	});
}

bool is_sha256_identifier(const std::string_view value) noexcept
{
	return value.size() == kSha256IdentifierBytes && value.substr(0, 7) == "sha256:" &&
	       std::all_of(value.begin() + 7, value.end(), [](const char character) {
		       return (character >= '0' && character <= '9') ||
			      (character >= 'a' && character <= 'f');
	       });
}

bool is_valid_presentation_binding(const TouchPresentationBinding &binding) noexcept
{
	return bhp::touch_observation_protocol::validate_lifecycle_presentation_binding(binding)
		.has_value();
}

bool presentation_bindings_match(const TouchPresentationBinding &left,
				 const TouchPresentationBinding &right) noexcept
{
	return bhp::touch_observation_protocol::lifecycle_presentation_bindings_equal(left,
									 right);
}

TouchObservationInvalidation
classify_presentation_change(const TouchPresentationBinding &previous,
			     const TouchPresentationBinding &next) noexcept
{
	if (previous.supervisor_session_id != next.supervisor_session_id) {
		return TouchObservationInvalidation::SupervisorRestart;
	}
	if (previous.ui_connection_challenge != next.ui_connection_challenge) {
		return TouchObservationInvalidation::UiDisconnected;
	}
	if (previous.lifecycle_protocol_version != next.lifecycle_protocol_version ||
	    previous.lifecycle_event_sequence != next.lifecycle_event_sequence ||
	    previous.lifecycle_snapshot_id != next.lifecycle_snapshot_id) {
		return TouchObservationInvalidation::NewLifecycleSnapshot;
	}
	return TouchObservationInvalidation::NewCommittedRenderGeneration;
}

TouchObservationInvalidation invalidation_reason_from_state(const std::uint64_t state) noexcept
{
	return static_cast<TouchObservationInvalidation>(state & kInvalidationReasonMask);
}

} // namespace

std::string_view to_string(const TouchObservationErrorReason reason) noexcept
{
	switch (reason) {
	case TouchObservationErrorReason::InvalidConfiguration:
		return "invalid_configuration";
	case TouchObservationErrorReason::InvalidPresentationBinding:
		return "invalid_presentation_binding";
	case TouchObservationErrorReason::NoCommittedPresentation:
		return "no_committed_presentation";
	case TouchObservationErrorReason::ObservationIdGenerationFailed:
		return "observation_id_generation_failed";
	case TouchObservationErrorReason::ObservationIdInvalid:
		return "observation_id_invalid";
	case TouchObservationErrorReason::ObservationIdCollision:
		return "observation_id_collision";
	case TouchObservationErrorReason::ObservationSequenceExhausted:
		return "observation_sequence_exhausted";
	case TouchObservationErrorReason::ObservationCapacityExceeded:
		return "observation_capacity_exceeded";
	case TouchObservationErrorReason::ContactAlreadyActive:
		return "contact_already_active";
	case TouchObservationErrorReason::ContactNotActive:
		return "contact_not_active";
	case TouchObservationErrorReason::ContactDurationInvalid:
		return "contact_duration_invalid";
	case TouchObservationErrorReason::ObservationUnknown:
		return "observation_unknown";
	case TouchObservationErrorReason::ObservationExpired:
		return "observation_expired";
	case TouchObservationErrorReason::ObservationAlreadyConsumed:
		return "observation_already_consumed";
	case TouchObservationErrorReason::ObservationInvalidated:
		return "observation_invalidated";
	case TouchObservationErrorReason::PresentationMismatch:
		return "presentation_mismatch";
	case TouchObservationErrorReason::AudienceInvalid:
		return "audience_invalid";
	case TouchObservationErrorReason::OperationDigestInvalid:
		return "operation_digest_invalid";
	case TouchObservationErrorReason::QualifyingHoldInvalid:
		return "qualifying_hold_invalid";
	case TouchObservationErrorReason::QualifyingHoldNotMet:
		return "qualifying_hold_not_met";
	case TouchObservationErrorReason::MonotonicClockReadFailed:
		return "monotonic_clock_read_failed";
	case TouchObservationErrorReason::MonotonicClockRegressed:
		return "monotonic_clock_regressed";
	case TouchObservationErrorReason::MonotonicDeadlineOverflow:
		return "monotonic_deadline_overflow";
	}
	return "unrecognized_touch_observation_error";
}

std::string_view to_string(const TouchObservationInvalidation reason) noexcept
{
	switch (reason) {
	case TouchObservationInvalidation::NewLifecycleSnapshot:
		return "new_lifecycle_snapshot";
	case TouchObservationInvalidation::NewCommittedRenderGeneration:
		return "new_committed_render_generation";
	case TouchObservationInvalidation::UiDisconnected:
		return "ui_disconnected";
	case TouchObservationInvalidation::Absence:
		return "absence";
	case TouchObservationInvalidation::Night:
		return "night";
	case TouchObservationInvalidation::SuspendPreparation:
		return "suspend_preparation";
	case TouchObservationInvalidation::SupervisorRestart:
		return "supervisor_restart";
	case TouchObservationInvalidation::Expired:
		return "expired";
	case TouchObservationInvalidation::PresentationMismatch:
		return "presentation_mismatch";
	case TouchObservationInvalidation::QualifyingHoldNotMet:
		return "qualifying_hold_not_met";
	case TouchObservationInvalidation::MonotonicClockFailure:
		return "monotonic_clock_failure";
	}
	return "unrecognized_touch_observation_invalidation";
}

std::string_view to_string(const TouchCapabilityHolderAudience audience) noexcept
{
	switch (audience) {
	case TouchCapabilityHolderAudience::MediaBroker:
		return "media_broker";
	case TouchCapabilityHolderAudience::PersonalContextClient:
		return "personal_context_client";
	}
	return "unrecognized_capability_holder";
}

Result<std::unique_ptr<TouchObservationAuthority>> TouchObservationAuthority::create(
	TouchObservationAuthorityConfiguration configuration,
	TouchObservationIdGenerator observation_id_generator,
	TouchObservationMonotonicClock monotonic_clock)
{
	if (configuration.observation_ttl.count() <= 0 ||
	    configuration.observation_ttl > kMaximumTouchObservationTtl ||
	    configuration.maximum_contact_duration.count() <= 0 ||
	    configuration.maximum_retained_observations == 0 || !observation_id_generator ||
	    !monotonic_clock) {
		return Result<std::unique_ptr<TouchObservationAuthority>>::failure(make_error(
			TouchObservationErrorReason::InvalidConfiguration, "create",
			"Failed to create touch observation authority because explicit bounds or injected ports are invalid",
			ErrorCode::InvalidConfiguration));
	}
	return Result<std::unique_ptr<TouchObservationAuthority>>::success(
		std::unique_ptr<TouchObservationAuthority>(new TouchObservationAuthority(
			std::move(configuration), std::move(observation_id_generator),
			std::move(monotonic_clock))));
}

TouchObservationAuthority::TouchObservationAuthority(
	TouchObservationAuthorityConfiguration configuration,
	TouchObservationIdGenerator observation_id_generator,
	TouchObservationMonotonicClock monotonic_clock)
	: configuration_(std::move(configuration)),
	  observation_id_generator_(std::move(observation_id_generator)),
	  monotonic_clock_(std::move(monotonic_clock))
{
}

Result<std::chrono::steady_clock::time_point>
TouchObservationAuthority::read_monotonic_time(const std::string_view operation)
{
	std::chrono::steady_clock::time_point now;
	try {
		now = monotonic_clock_();
	} catch (const std::exception &) {
		invalidate_available(TouchObservationInvalidation::MonotonicClockFailure);
		current_presentation_binding_.reset();
		active_contact_measurement_.reset();
		return Result<std::chrono::steady_clock::time_point>::failure(make_error(
			TouchObservationErrorReason::MonotonicClockReadFailed, operation,
			"Failed to read the injected monotonic clock",
			ErrorCode::AdapterUnavailable));
	} catch (...) {
		invalidate_available(TouchObservationInvalidation::MonotonicClockFailure);
		current_presentation_binding_.reset();
		active_contact_measurement_.reset();
		return Result<std::chrono::steady_clock::time_point>::failure(make_error(
			TouchObservationErrorReason::MonotonicClockReadFailed, operation,
			"Failed to read the injected monotonic clock",
			ErrorCode::AdapterUnavailable));
	}
	if (latest_monotonic_time_.has_value() && now < *latest_monotonic_time_) {
		invalidate_available(TouchObservationInvalidation::MonotonicClockFailure);
		current_presentation_binding_.reset();
		active_contact_measurement_.reset();
		return Result<std::chrono::steady_clock::time_point>::failure(make_error(
			TouchObservationErrorReason::MonotonicClockRegressed, operation,
			"Rejected touch observation operation because monotonic time regressed",
			ErrorCode::ClockMovedBackward));
	}
	latest_monotonic_time_ = now;
	return Result<std::chrono::steady_clock::time_point>::success(now);
}

Result<void> TouchObservationAuthority::record_committed_presentation(
	TouchPresentationBinding presentation_binding)
{
	const std::lock_guard<std::mutex> lock(mutex_);
	reconcile_external_invalidation();
	const auto operation_invalidation_state = current_invalidation_state_;
	if (!is_valid_presentation_binding(presentation_binding)) {
		return Result<void>::failure(make_error(
			TouchObservationErrorReason::InvalidPresentationBinding,
			"record_committed_presentation",
			"Failed to record committed presentation because its provenance binding is incomplete"));
	}
	if (current_presentation_binding_.has_value() &&
	    !presentation_bindings_match(*current_presentation_binding_, presentation_binding)) {
		invalidate_available(classify_presentation_change(*current_presentation_binding_,
							       presentation_binding));
		active_contact_measurement_.reset();
	}
	if (external_invalidation_state_.load(std::memory_order_acquire) !=
	    operation_invalidation_state) {
		reconcile_external_invalidation();
		return Result<void>::failure(make_error(
			TouchObservationErrorReason::ObservationInvalidated,
			"record_committed_presentation",
			"Failed to record committed presentation because lifecycle invalidation raced the update"));
	}
	current_presentation_binding_ = std::move(presentation_binding);
	if (external_invalidation_state_.load(std::memory_order_acquire) !=
	    operation_invalidation_state) {
		reconcile_external_invalidation();
		return Result<void>::failure(make_error(
			TouchObservationErrorReason::ObservationInvalidated,
			"record_committed_presentation",
			"Failed to record committed presentation because lifecycle invalidation raced the update"));
	}
	return Result<void>::success();
}

Result<void> TouchObservationAuthority::make_capacity_for_new_observation(
	const std::chrono::steady_clock::time_point now)
{
	if (observations_.size() < configuration_.maximum_retained_observations) {
		return Result<void>::success();
	}

	auto reclaimable = observations_.end();
	for (auto candidate = observations_.begin(); candidate != observations_.end(); ++candidate) {
		const bool terminal = candidate->second.state != StoredState::Available ||
				      now >= candidate->second.receipt.expires_at;
		if (!terminal) {
			continue;
		}
		if (reclaimable == observations_.end() ||
		    candidate->second.receipt.contact_completed_at <
			    reclaimable->second.receipt.contact_completed_at) {
			reclaimable = candidate;
		}
	}
	if (reclaimable == observations_.end()) {
		return Result<void>::failure(make_error(
			TouchObservationErrorReason::ObservationCapacityExceeded,
			"record_contact_completed",
			"Failed to retain touch observation because every bounded slot holds live evidence"));
	}
	observations_.erase(reclaimable);
	return Result<void>::success();
}

Result<void> TouchObservationAuthority::record_contact_started()
{
	const std::lock_guard<std::mutex> lock(mutex_);
	reconcile_external_invalidation();
	const auto operation_invalidation_state = current_invalidation_state_;
	if (!current_presentation_binding_.has_value()) {
		return Result<void>::failure(make_error(
			TouchObservationErrorReason::NoCommittedPresentation,
			"record_contact_started",
			"Failed to start contact measurement because no current committed presentation exists"));
	}
	if (active_contact_measurement_.has_value()) {
		return Result<void>::failure(make_error(
			TouchObservationErrorReason::ContactAlreadyActive,
			"record_contact_started",
			"Failed to start contact measurement because another contact is already active"));
	}

	auto started_at_result = read_monotonic_time("record_contact_started");
	if (!started_at_result) {
		return Result<void>::failure(started_at_result.error());
	}
	const auto started_at = started_at_result.value();
	if (started_at > std::chrono::steady_clock::time_point::max() -
			 configuration_.maximum_contact_duration) {
		return Result<void>::failure(make_error(
			TouchObservationErrorReason::MonotonicDeadlineOverflow,
			"record_contact_started",
			"Failed to start contact measurement because its maximum completion time is not representable"));
	}
	if (external_invalidation_state_.load(std::memory_order_acquire) !=
	    operation_invalidation_state) {
		reconcile_external_invalidation();
		return Result<void>::failure(make_error(
			TouchObservationErrorReason::ObservationInvalidated,
			"record_contact_started",
			"Failed to start contact measurement because lifecycle invalidation raced the clock read"));
	}

	active_contact_measurement_ =
		ActiveContactMeasurement{started_at, *current_presentation_binding_};
	if (external_invalidation_state_.load(std::memory_order_acquire) !=
	    operation_invalidation_state) {
		reconcile_external_invalidation();
		return Result<void>::failure(make_error(
			TouchObservationErrorReason::ObservationInvalidated,
			"record_contact_started",
			"Failed to start contact measurement because lifecycle invalidation raced the update"));
	}
	return Result<void>::success();
}

Result<TouchObservationReceipt> TouchObservationAuthority::record_contact_completed()
{
	const std::lock_guard<std::mutex> lock(mutex_);
	reconcile_external_invalidation();
	const auto operation_invalidation_state = current_invalidation_state_;
	if (!active_contact_measurement_.has_value()) {
		return Result<TouchObservationReceipt>::failure(make_error(
			TouchObservationErrorReason::ContactNotActive,
			"record_contact_completed",
			"Failed to complete contact measurement because no contact is active"));
	}

	auto completed_at_result = read_monotonic_time("record_contact_completed");
	if (!completed_at_result) {
		return Result<TouchObservationReceipt>::failure(completed_at_result.error());
	}
	const auto completed_at = completed_at_result.value();
	const auto started_at = active_contact_measurement_->started_at;
	const auto contact_presentation_binding = active_contact_measurement_->presentation_binding;
	active_contact_measurement_.reset();
	if (external_invalidation_state_.load(std::memory_order_acquire) !=
	    operation_invalidation_state) {
		reconcile_external_invalidation();
		return Result<TouchObservationReceipt>::failure(make_error(
			TouchObservationErrorReason::ObservationInvalidated,
			"record_contact_completed",
			"Failed to complete contact measurement because lifecycle invalidation raced the clock read"));
	}
	if (!current_presentation_binding_.has_value() ||
	    !presentation_bindings_match(contact_presentation_binding,
					 *current_presentation_binding_)) {
		return Result<TouchObservationReceipt>::failure(make_error(
			TouchObservationErrorReason::PresentationMismatch,
			"record_contact_completed",
			"Failed to complete contact measurement because its presentation binding changed"));
	}
	if (completed_at > started_at + configuration_.maximum_contact_duration) {
		return Result<TouchObservationReceipt>::failure(make_error(
			TouchObservationErrorReason::ContactDurationInvalid,
			"record_contact_completed",
			"Failed to record completed contact because authority-measured duration exceeds the explicit bound"));
	}
	const auto supervisor_measured_duration =
		std::chrono::duration_cast<std::chrono::milliseconds>(completed_at - started_at);
	if (supervisor_measured_duration.count() <= 0) {
		return Result<TouchObservationReceipt>::failure(make_error(
			TouchObservationErrorReason::ContactDurationInvalid,
			"record_contact_completed",
			"Failed to record completed contact because authority-measured duration is not positive"));
	}

	const auto now = completed_at;
	auto capacity_result = make_capacity_for_new_observation(now);
	if (!capacity_result) {
		return Result<TouchObservationReceipt>::failure(capacity_result.error());
	}
	if (latest_observation_sequence_ == std::numeric_limits<std::uint64_t>::max()) {
		return Result<TouchObservationReceipt>::failure(make_error(
			TouchObservationErrorReason::ObservationSequenceExhausted,
			"record_contact_completed",
			"Failed to record completed contact because observation sequence is exhausted"));
	}
	if (now > std::chrono::steady_clock::time_point::max() - configuration_.observation_ttl) {
		return Result<TouchObservationReceipt>::failure(make_error(
			TouchObservationErrorReason::MonotonicDeadlineOverflow,
			"record_contact_completed",
			"Failed to record completed contact because monotonic expiry is not representable"));
	}

	Result<std::string> generated_id = Result<std::string>::failure(make_error(
		TouchObservationErrorReason::ObservationIdGenerationFailed,
		"record_contact_completed", "Touch observation ID generator did not run",
		ErrorCode::AdapterUnavailable));
	try {
		generated_id = observation_id_generator_();
	} catch (const std::exception &) {
		return Result<TouchObservationReceipt>::failure(make_error(
			TouchObservationErrorReason::ObservationIdGenerationFailed,
			"record_contact_completed",
			"Failed to generate unpredictable touch observation identity",
			ErrorCode::AdapterUnavailable));
	} catch (...) {
		return Result<TouchObservationReceipt>::failure(make_error(
			TouchObservationErrorReason::ObservationIdGenerationFailed,
			"record_contact_completed",
			"Failed to generate unpredictable touch observation identity",
			ErrorCode::AdapterUnavailable));
	}
	if (!generated_id) {
		return Result<TouchObservationReceipt>::failure(make_error(
			TouchObservationErrorReason::ObservationIdGenerationFailed,
			"record_contact_completed",
			"Failed to generate unpredictable touch observation identity",
			ErrorCode::AdapterUnavailable));
	}
	if (!is_safe_opaque_text(generated_id.value(), kMinimumObservationIdBytes,
				 kMaximumObservationIdBytes)) {
		return Result<TouchObservationReceipt>::failure(make_error(
			TouchObservationErrorReason::ObservationIdInvalid,
			"record_contact_completed",
			"Failed to record completed contact because generated identity violates the opaque-ID contract",
			ErrorCode::AdapterUnavailable));
	}
	if (observations_.find(generated_id.value()) != observations_.end()) {
		return Result<TouchObservationReceipt>::failure(make_error(
			TouchObservationErrorReason::ObservationIdCollision,
			"record_contact_completed",
			"Failed to record completed contact because generated identity collided with retained state",
			ErrorCode::AdapterUnavailable));
	}
	if (external_invalidation_state_.load(std::memory_order_acquire) !=
	    operation_invalidation_state) {
		reconcile_external_invalidation();
		return Result<TouchObservationReceipt>::failure(make_error(
			TouchObservationErrorReason::ObservationInvalidated,
			"record_contact_completed",
			"Failed to record completed contact because lifecycle invalidation raced identity creation"));
	}

	TouchObservationReceipt receipt{
		kTouchObservationProtocolVersion, generated_id.value(),
		latest_observation_sequence_ + 1, contact_presentation_binding, now,
		supervisor_measured_duration, now + configuration_.observation_ttl};
	++latest_observation_sequence_;
	const auto inserted = observations_.emplace(
		receipt.touch_observation_id,
		StoredObservation{receipt, StoredState::Available, std::nullopt});
	if (external_invalidation_state_.load(std::memory_order_acquire) !=
	    operation_invalidation_state) {
		inserted.first->second.state = StoredState::Invalidated;
		inserted.first->second.invalidation_reason = invalidation_reason_from_state(
			external_invalidation_state_.load(std::memory_order_acquire));
		reconcile_external_invalidation();
		return Result<TouchObservationReceipt>::failure(make_error(
			TouchObservationErrorReason::ObservationInvalidated,
			"record_contact_completed",
			"Failed to record completed contact because lifecycle invalidation raced identity creation"));
	}
	return Result<TouchObservationReceipt>::success(std::move(receipt));
}

Result<TouchProvenanceEnvelope>
TouchObservationAuthority::redeem_for_media_broker(const TouchObservationClaim &claim)
{
	return redeem_observation(claim, TouchCapabilityHolderAudience::MediaBroker);
}

Result<TouchProvenanceEnvelope>
TouchObservationAuthority::redeem_for_personal_context_client(
	const TouchObservationClaim &claim)
{
	return redeem_observation(claim,
				  TouchCapabilityHolderAudience::PersonalContextClient);
}

Result<TouchProvenanceEnvelope> TouchObservationAuthority::redeem_observation(
	const TouchObservationClaim &claim,
	const TouchCapabilityHolderAudience capability_holder_audience)
{
	const std::lock_guard<std::mutex> lock(mutex_);
	reconcile_external_invalidation();
	const auto operation_invalidation_state = current_invalidation_state_;
	if (!is_sha256_identifier(claim.canonical_operation_digest)) {
		return Result<TouchProvenanceEnvelope>::failure(make_error(
			TouchObservationErrorReason::OperationDigestInvalid, "redeem_observation",
			"Failed to redeem touch observation because canonical operation digest is invalid"));
	}
	if (claim.qualifying_hold_threshold.has_value() &&
	    (claim.qualifying_hold_threshold->count() <= 0 ||
	     *claim.qualifying_hold_threshold > configuration_.maximum_contact_duration)) {
		return Result<TouchProvenanceEnvelope>::failure(make_error(
			TouchObservationErrorReason::QualifyingHoldInvalid, "redeem_observation",
			"Failed to redeem touch observation because qualifying hold threshold is invalid"));
	}

	auto observation = observations_.find(claim.touch_observation_id);
	if (observation == observations_.end()) {
		return Result<TouchProvenanceEnvelope>::failure(make_error(
			TouchObservationErrorReason::ObservationUnknown, "redeem_observation",
			"Failed to redeem touch observation because no retained observation matches"));
	}
	if (observation->second.state == StoredState::Consumed) {
		return Result<TouchProvenanceEnvelope>::failure(make_error(
			TouchObservationErrorReason::ObservationAlreadyConsumed, "redeem_observation",
			"Failed to redeem touch observation because it was already consumed"));
	}
	if (observation->second.state == StoredState::Invalidated) {
		return Result<TouchProvenanceEnvelope>::failure(make_error(
			TouchObservationErrorReason::ObservationInvalidated, "redeem_observation",
			"Failed to redeem touch observation because lifecycle state invalidated it",
			ErrorCode::InvalidPolicyObservation,
			{{"invalidation_reason",
			  std::string(to_string(*observation->second.invalidation_reason))}}));
	}

	auto now_result = read_monotonic_time("redeem_observation");
	if (!now_result) {
		return Result<TouchProvenanceEnvelope>::failure(now_result.error());
	}
	const auto now = now_result.value();
	if (now >= observation->second.receipt.expires_at) {
		observation->second.state = StoredState::Invalidated;
		observation->second.invalidation_reason = TouchObservationInvalidation::Expired;
		return Result<TouchProvenanceEnvelope>::failure(make_error(
			TouchObservationErrorReason::ObservationExpired, "redeem_observation",
			"Failed to redeem touch observation because its monotonic deadline expired"));
	}

	const bool presentation_matches = current_presentation_binding_.has_value() &&
		presentation_bindings_match(observation->second.receipt.presentation_binding,
					    *current_presentation_binding_) &&
		presentation_bindings_match(observation->second.receipt.presentation_binding,
					    claim.expected_presentation_binding);
	if (!presentation_matches) {
		observation->second.state = StoredState::Invalidated;
		observation->second.invalidation_reason =
			TouchObservationInvalidation::PresentationMismatch;
		return Result<TouchProvenanceEnvelope>::failure(make_error(
			TouchObservationErrorReason::PresentationMismatch, "redeem_observation",
			"Failed to redeem touch observation because full presentation provenance does not match"));
	}
	if (claim.qualifying_hold_threshold.has_value() &&
	    observation->second.receipt.supervisor_measured_contact_duration <
		    *claim.qualifying_hold_threshold) {
		observation->second.state = StoredState::Invalidated;
		observation->second.invalidation_reason =
			TouchObservationInvalidation::QualifyingHoldNotMet;
		return Result<TouchProvenanceEnvelope>::failure(make_error(
			TouchObservationErrorReason::QualifyingHoldNotMet, "redeem_observation",
			"Failed to redeem touch observation because supervisor-measured hold did not qualify"));
	}

	TouchProvenanceEnvelope envelope{
		kTouchObservationProtocolVersion,
		observation->second.receipt.touch_observation_id,
		observation->second.receipt.observation_sequence,
		capability_holder_audience,
		claim.canonical_operation_digest,
		observation->second.receipt.presentation_binding,
		observation->second.receipt.contact_completed_at,
		observation->second.receipt.supervisor_measured_contact_duration,
		observation->second.receipt.expires_at,
		claim.qualifying_hold_threshold,
		true,
	};
	observation->second.state = StoredState::Consumed;
	if (external_invalidation_state_.load(std::memory_order_acquire) !=
	    operation_invalidation_state) {
		observation->second.state = StoredState::Invalidated;
		observation->second.invalidation_reason = invalidation_reason_from_state(
			external_invalidation_state_.load(std::memory_order_acquire));
		reconcile_external_invalidation();
		return Result<TouchProvenanceEnvelope>::failure(make_error(
			TouchObservationErrorReason::ObservationInvalidated, "redeem_observation",
			"Failed to redeem touch observation because lifecycle invalidation raced consumption"));
	}
	return Result<TouchProvenanceEnvelope>::success(std::move(envelope));
}

void TouchObservationAuthority::invalidate(const TouchObservationInvalidation reason) noexcept
{
	auto previous_state = external_invalidation_state_.load(std::memory_order_relaxed);
	std::uint64_t next_state = 0;
	do {
		const auto next_sequence = (previous_state >> 8U) + 1U;
		next_state = (next_sequence << 8U) |
			     static_cast<std::uint64_t>(reason);
	} while (!external_invalidation_state_.compare_exchange_weak(
		previous_state, next_state, std::memory_order_release,
		std::memory_order_relaxed));
}

void TouchObservationAuthority::invalidate_available(
	const TouchObservationInvalidation reason) noexcept
{
	for (auto &[observation_id, observation] : observations_) {
		(void)observation_id;
		if (observation.state == StoredState::Available) {
			observation.state = StoredState::Invalidated;
			observation.invalidation_reason = reason;
		}
	}
}

void TouchObservationAuthority::reconcile_external_invalidation() noexcept
{
	const auto observed_state = external_invalidation_state_.load(std::memory_order_acquire);
	if (observed_state == current_invalidation_state_) {
		return;
	}
	invalidate_available(invalidation_reason_from_state(observed_state));
	current_presentation_binding_.reset();
	active_contact_measurement_.reset();
	current_invalidation_state_ = observed_state;
}

std::size_t TouchObservationAuthority::retained_observation_count()
{
	const std::lock_guard<std::mutex> lock(mutex_);
	reconcile_external_invalidation();
	return observations_.size();
}

std::size_t TouchObservationAuthority::available_observation_count()
{
	const std::lock_guard<std::mutex> lock(mutex_);
	reconcile_external_invalidation();
	return static_cast<std::size_t>(std::count_if(
		observations_.begin(), observations_.end(), [](const auto &entry) {
			return entry.second.state == StoredState::Available;
		}));
}

} // namespace house_panel::supervisor
