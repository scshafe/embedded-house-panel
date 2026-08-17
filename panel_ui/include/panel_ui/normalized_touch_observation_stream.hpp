#pragma once

#include "panel_ui/result.hpp"

#include "bhp/touch_observation_protocol/types.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace house_panel::panel_ui {

using NormalizedTouchContactState =
	bhp::touch_observation_protocol::ContactState;
using NormalizedTouchObservation =
	bhp::touch_observation_protocol::NormalizedTouchObservation;

// @extension-point normalized_touch_observation_stream -- normalized evidence, never raw evdev.
class NormalizedTouchObservationStream {
public:
	virtual ~NormalizedTouchObservationStream() = default;
	[[nodiscard]] virtual PanelUiResult<std::optional<NormalizedTouchObservation>>
	receive_until(std::chrono::steady_clock::time_point deadline) = 0;
};

} // namespace house_panel::panel_ui
