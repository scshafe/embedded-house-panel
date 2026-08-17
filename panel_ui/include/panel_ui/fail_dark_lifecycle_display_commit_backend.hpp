#pragma once

#include "panel_ui/lifecycle_display_commit_backend.hpp"

namespace house_panel::panel_ui {

// @canonical fail_dark_lifecycle_display_commit_backend -- explicit negative presentation until
// an evidence-backed physical display commit backend exists.
class FailDarkLifecycleDisplayCommitBackend final : public LifecycleDisplayCommitBackend {
public:
	[[nodiscard]] PanelUiResult<LifecycleRenderCommit>
	commit(const bhp::panel_mode_protocol::Snapshot &snapshot,
	       const HeadlessRenderFrame &validated_frame) override;
};

} // namespace house_panel::panel_ui
