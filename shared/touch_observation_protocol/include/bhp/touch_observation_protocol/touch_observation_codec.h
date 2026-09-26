#pragma once
#include "bhp/touch_observation_protocol/result.h"
#include "bhp/touch_observation_protocol/types.h"
#include <string_view>

namespace bhp::touch_observation_protocol {
inline constexpr std::size_t kMaximumTouchFrameBytes = 2048;
/** Strict canonical JSON-Lines wire format; coordinates never enter diagnostics. */
[[nodiscard]] Result<std::string> encode_touch_observation(const NormalizedTouchObservation &value);
/** Accepts one bounded canonical frame including its LF. Rejects unknown/duplicate fields. */
[[nodiscard]] Result<NormalizedTouchObservation> decode_touch_observation(std::string_view frame);
}
