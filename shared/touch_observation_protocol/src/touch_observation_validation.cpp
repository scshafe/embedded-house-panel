#include "bhp/touch_observation_protocol/touch_observation_validation.h"

#include <algorithm>
#include <map>
#include <string>
#include <string_view>
#include <utility>

namespace bhp::touch_observation_protocol {
namespace {

Error make_field_error(const ErrorCode code, std::string message,
		       const std::string_view field, const std::string_view operation,
		       std::map<std::string, std::string> extra_context = {})
{
	extra_context.emplace("field", field);
	extra_context.emplace("operation", operation);
	return Error{code, std::move(message), std::move(extra_context)};
}

bool is_nonzero_lower_hex(const std::string_view value,
			  const std::size_t expected_bytes) noexcept
{
	return value.size() == expected_bytes * 2U &&
	       std::all_of(value.begin(), value.end(), [](const char character) {
		       return (character >= '0' && character <= '9') ||
			      (character >= 'a' && character <= 'f');
	       }) &&
	       std::any_of(value.begin(), value.end(),
			   [](const char character) { return character != '0'; });
}

bool is_sha256_identifier(const std::string_view value) noexcept
{
	return value.size() == 71U && value.substr(0, 7U) == "sha256:" &&
	       std::all_of(value.begin() + 7, value.end(), [](const char character) {
		       return (character >= '0' && character <= '9') ||
			      (character >= 'a' && character <= 'f');
	       });
}

bool is_safe_opaque_identifier(const std::string_view value) noexcept
{
	if (value.size() < kMinimumTouchObservationIdBytes ||
	    value.size() > kMaximumTouchObservationIdBytes) {
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

bool is_valid_contact_state(const ContactState state) noexcept
{
	return state == ContactState::Started || state == ContactState::Updated ||
	       state == ContactState::Completed;
}

bool is_safe_signed_protocol_integer(const std::int64_t value) noexcept
{
	constexpr auto kMaximum =
		static_cast<std::int64_t>(panel_mode_protocol::kMaximumSafeJsonInteger);
	return value >= -kMaximum && value <= kMaximum;
}

} // namespace

Result<void> validate_lifecycle_presentation_binding(
	const LifecyclePresentationBinding &binding)
{
	constexpr std::string_view kOperation = "validate_lifecycle_presentation_binding";
	if (binding.lifecycle_protocol_version != panel_mode_protocol::kProtocolVersion) {
		return Result<void>::failure(make_field_error(
			ErrorCode::LifecycleBindingInvalid,
			"Lifecycle presentation binding has an unsupported protocol version",
			"lifecycle_protocol_version", kOperation));
	}
	if (!is_nonzero_lower_hex(binding.supervisor_session_id, 16U)) {
		return Result<void>::failure(make_field_error(
			ErrorCode::LifecycleBindingInvalid,
			"Lifecycle presentation binding has an invalid supervisor session",
			"supervisor_session_id", kOperation));
	}
	if (binding.lifecycle_event_sequence == 0U ||
	    binding.lifecycle_event_sequence > panel_mode_protocol::kMaximumSafeJsonInteger) {
		return Result<void>::failure(make_field_error(
			ErrorCode::LifecycleBindingInvalid,
			"Lifecycle presentation binding has an invalid event sequence",
			"lifecycle_event_sequence", kOperation,
			{{"bound", "1..maximum_safe_json_integer"}}));
	}
	if (!is_sha256_identifier(binding.lifecycle_snapshot_id)) {
		return Result<void>::failure(make_field_error(
			ErrorCode::LifecycleBindingInvalid,
			"Lifecycle presentation binding has an invalid snapshot digest",
			"lifecycle_snapshot_id", kOperation));
	}
	if (!is_nonzero_lower_hex(binding.ui_connection_challenge, 16U)) {
		return Result<void>::failure(make_field_error(
			ErrorCode::LifecycleBindingInvalid,
			"Lifecycle presentation binding has an invalid UI challenge",
			"ui_connection_challenge", kOperation));
	}
	if (binding.committed_render_generation == 0U ||
	    binding.committed_render_generation >
		    panel_mode_protocol::kMaximumSafeJsonInteger) {
		return Result<void>::failure(make_field_error(
			ErrorCode::LifecycleBindingInvalid,
			"Lifecycle presentation binding has an invalid render generation",
			"committed_render_generation", kOperation,
			{{"bound", "1..maximum_safe_json_integer"}}));
	}
	return Result<void>::success();
}

Result<void>
validate_normalized_touch_observation(const NormalizedTouchObservation &observation)
{
	constexpr std::string_view kOperation = "validate_normalized_touch_observation";
	if (observation.touch_observation_protocol_version !=
	    kTouchObservationProtocolVersion) {
		return Result<void>::failure(make_field_error(
			ErrorCode::TouchProtocolUnsupported,
			"Touch observation has an unsupported protocol version",
			"touch_observation_protocol_version", kOperation));
	}
	if (!is_safe_opaque_identifier(observation.touch_observation_id)) {
		return Result<void>::failure(make_field_error(
			ErrorCode::ObservationIdInvalid,
			"Touch observation has an invalid opaque identifier",
			"touch_observation_id", kOperation,
			{{"bound", "22..128_safe_ascii_bytes"}}));
	}
	if (observation.observation_sequence == 0U ||
	    observation.observation_sequence >
		    panel_mode_protocol::kMaximumSafeJsonInteger) {
		return Result<void>::failure(make_field_error(
			ErrorCode::ObservationSequenceInvalid,
			"Touch observation has an invalid sequence",
			"observation_sequence", kOperation,
			{{"bound", "1..maximum_safe_json_integer"}}));
	}
	auto presentation_result =
		validate_lifecycle_presentation_binding(observation.presentation_binding);
	if (!presentation_result) {
		return presentation_result;
	}
	if (!is_safe_signed_protocol_integer(observation.raw_axis_x)) {
		return Result<void>::failure(make_field_error(
			ErrorCode::RawAxisInvalid,
			"Touch observation raw axis is outside the protocol integer range",
			"raw_axis_x", kOperation,
			{{"bound", "signed_maximum_safe_json_integer"}}));
	}
	if (!is_safe_signed_protocol_integer(observation.raw_axis_y)) {
		return Result<void>::failure(make_field_error(
			ErrorCode::RawAxisInvalid,
			"Touch observation raw axis is outside the protocol integer range",
			"raw_axis_y", kOperation,
			{{"bound", "signed_maximum_safe_json_integer"}}));
	}
	if (!is_valid_contact_state(observation.contact_state)) {
		return Result<void>::failure(make_field_error(
			ErrorCode::ContactStateInvalid,
			"Touch observation has an invalid contact state", "contact_state",
			kOperation));
	}
	const bool completed = observation.contact_state == ContactState::Completed;
	const auto duration =
		observation.supervisor_measured_contact_duration_milliseconds;
	if ((completed && duration == 0U) || (!completed && duration != 0U) ||
	    duration > panel_mode_protocol::kMaximumSafeJsonInteger) {
		return Result<void>::failure(make_field_error(
			ErrorCode::ContactDurationInvalid,
			"Touch observation has a duration inconsistent with its contact state",
			"supervisor_measured_contact_duration_milliseconds", kOperation,
			{{"bound", "zero_until_completed_then_1..maximum_safe_json_integer"}}));
	}
	return Result<void>::success();
}

bool lifecycle_presentation_bindings_equal(
	const LifecyclePresentationBinding &left,
	const LifecyclePresentationBinding &right) noexcept
{
	return left.lifecycle_protocol_version == right.lifecycle_protocol_version &&
	       left.supervisor_session_id == right.supervisor_session_id &&
	       left.lifecycle_event_sequence == right.lifecycle_event_sequence &&
	       left.lifecycle_snapshot_id == right.lifecycle_snapshot_id &&
	       left.ui_connection_challenge == right.ui_connection_challenge &&
	       left.committed_render_generation == right.committed_render_generation;
}

} // namespace bhp::touch_observation_protocol
