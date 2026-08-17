#include "panel_ui/headless_renderer.hpp"

#include <iostream>
#include <string>

namespace {

using namespace house_panel::panel_ui;

int test_headless_renderer_preserves_neutral_model_and_advances_generation()
{
	auto renderer_result = HeadlessRenderer::create({64, 24, true});
	if (!renderer_result) {
		return 1;
	}
	const ShellRenderModel model{LifecycleMode::DayActive, ShellTransitionState::Stable,
				     "fixture_surface", {u8"Здравствуйте"}};
	const auto first = renderer_result.value()->render(model);
	const auto second = renderer_result.value()->render(model);
	return first && second && first.value().render_generation == 1 &&
		       second.value().render_generation == 2 &&
		       second.value().active_surface_id == "fixture_surface" &&
		       second.value().visible_text_runs == model.visible_text_runs
		       ? 0
		       : 1;
}

int test_headless_renderer_rejects_text_outside_unicode_policy()
{
	auto renderer_result = HeadlessRenderer::create({4, 4, false});
	const ShellRenderModel model{LifecycleMode::Diagnostic, ShellTransitionState::Stable,
				     "diagnostic_surface", {"oversized"}};
	const auto result = renderer_result.value()->render(model);
	return !result && result.error().code() == PanelUiErrorCode::UnicodeByteLimitExceeded ? 0
											      : 1;
}

} // namespace

int main()
{
	if (test_headless_renderer_preserves_neutral_model_and_advances_generation() != 0) {
		std::cerr << "test_headless_renderer_preserves_neutral_model_and_advances_generation failed\n";
		return 1;
	}
	if (test_headless_renderer_rejects_text_outside_unicode_policy() != 0) {
		std::cerr << "test_headless_renderer_rejects_text_outside_unicode_policy failed\n";
		return 1;
	}
	return 0;
}
