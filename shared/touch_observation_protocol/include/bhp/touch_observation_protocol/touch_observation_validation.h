#pragma once

#include "bhp/touch_observation_protocol/result.h"
#include "bhp/touch_observation_protocol/types.h"

namespace bhp::touch_observation_protocol {

/** Validates a complete committed-render binding without exposing supplied identifiers. */
[[nodiscard]] Result<void> validate_lifecycle_presentation_binding(
	const LifecyclePresentationBinding &binding);

/** Validates one raw-axis observation and its complete presentation provenance. */
[[nodiscard]] Result<void>
validate_normalized_touch_observation(const NormalizedTouchObservation &observation);

/** Compares all provenance fields exactly after boundary validation. */
[[nodiscard]] bool lifecycle_presentation_bindings_equal(
	const LifecyclePresentationBinding &left,
	const LifecyclePresentationBinding &right) noexcept;

} // namespace bhp::touch_observation_protocol
