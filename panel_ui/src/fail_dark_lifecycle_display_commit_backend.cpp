#include "panel_ui/fail_dark_lifecycle_display_commit_backend.hpp"

namespace house_panel::panel_ui {

PanelUiResult<LifecycleRenderCommit> FailDarkLifecycleDisplayCommitBackend::commit(
	const bhp::panel_mode_protocol::Snapshot &snapshot,
	const HeadlessRenderFrame &validated_frame)
{
	(void)snapshot;
	return PanelUiResult<LifecycleRenderCommit>::success(LifecycleRenderCommit{
		validated_frame.render_generation,
		bhp::panel_mode_protocol::PresentationResult::Failed});
}

} // namespace house_panel::panel_ui
