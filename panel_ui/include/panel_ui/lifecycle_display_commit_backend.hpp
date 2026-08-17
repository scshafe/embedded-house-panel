#pragma once

#include "panel_ui/headless_renderer.hpp"
#include "panel_ui/lifecycle_presentation_connection.hpp"
#include "panel_ui/result.hpp"

#include "bhp/panel_mode_protocol/types.h"

namespace house_panel::panel_ui {

// @extension-point lifecycle_display_commit_backend -- replace fail-dark presentation with one
// evidence-backed physical commit primitive without changing lifecycle policy or feature routing.
class LifecycleDisplayCommitBackend {
public:
	virtual ~LifecycleDisplayCommitBackend() = default;

	/**
	 * Attempts one exact rendered generation. `committed` is permitted only after
	 * physical commit completion; `failed` is the explicit fail-dark result.
	 */
	[[nodiscard]] virtual PanelUiResult<LifecycleRenderCommit>
	commit(const bhp::panel_mode_protocol::Snapshot &snapshot,
	       const HeadlessRenderFrame &validated_frame) = 0;
};

} // namespace house_panel::panel_ui
