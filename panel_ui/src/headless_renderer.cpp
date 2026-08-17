#include "panel_ui/headless_renderer.hpp"

#include <limits>
#include <utility>

namespace house_panel::panel_ui {

PanelUiResult<std::unique_ptr<HeadlessRenderer>>
HeadlessRenderer::create(const UnicodeTextPolicyConfiguration unicode_configuration)
{
	auto policy_result = BoundedUnicodeTextPolicy::create(unicode_configuration);
	if (!policy_result) {
		return PanelUiResult<std::unique_ptr<HeadlessRenderer>>::failure(
			policy_result.error());
	}
	return PanelUiResult<std::unique_ptr<HeadlessRenderer>>::success(
		std::unique_ptr<HeadlessRenderer>(
			new HeadlessRenderer(std::move(policy_result).value())));
}

HeadlessRenderer::HeadlessRenderer(std::unique_ptr<BoundedUnicodeTextPolicy> unicode_policy)
	: unicode_policy_(std::move(unicode_policy))
{
}

PanelUiResult<HeadlessRenderFrame>
HeadlessRenderer::render(const ShellRenderModel &render_model)
{
	if (render_model.active_surface_id.empty()) {
		return PanelUiResult<HeadlessRenderFrame>::failure(PanelUiError(
			PanelUiErrorCode::ConfigurationSurfaceInvalid,
			"Headless render model requires an explicit active surface",
			{{"field", "active_surface_id"}}));
	}
	if (latest_render_generation_ == std::numeric_limits<std::uint64_t>::max()) {
		return PanelUiResult<HeadlessRenderFrame>::failure(PanelUiError(
			PanelUiErrorCode::RendererGenerationExhausted,
			"Headless renderer generation is exhausted",
			{{"field", "render_generation"}}));
	}

	std::vector<std::string> validated_text_runs;
	validated_text_runs.reserve(render_model.visible_text_runs.size());
	for (const auto &text_run : render_model.visible_text_runs) {
		auto validation_result = unicode_policy_->validate(text_run);
		if (!validation_result) {
			return PanelUiResult<HeadlessRenderFrame>::failure(validation_result.error());
		}
		validated_text_runs.push_back(std::move(validation_result).value());
	}

	++latest_render_generation_;
	return PanelUiResult<HeadlessRenderFrame>::success(HeadlessRenderFrame{
		latest_render_generation_, render_model.lifecycle_mode,
		render_model.transition_state, render_model.active_surface_id,
		std::move(validated_text_runs)});
}

} // namespace house_panel::panel_ui
