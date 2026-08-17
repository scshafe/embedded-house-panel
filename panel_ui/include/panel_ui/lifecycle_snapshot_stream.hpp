#pragma once

#include "bhp/panel_mode_protocol/types.h"
#include "panel_ui/result.hpp"

#include <chrono>
#include <optional>

namespace house_panel::panel_ui {

// @extension-point panel_lifecycle_snapshot_stream -- lifecycle input only; never feature traffic.
class LifecycleSnapshotStream {
public:
	virtual ~LifecycleSnapshotStream() = default;
	[[nodiscard]] virtual PanelUiResult<std::optional<bhp::panel_mode_protocol::Snapshot>>
	receive_until(std::chrono::steady_clock::time_point deadline) = 0;
};

} // namespace house_panel::panel_ui
