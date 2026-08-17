#pragma once

#include "panel_ui/bounded_unicode_text_policy.hpp"
#include "panel_ui/result.hpp"
#include "panel_ui/shell_state.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace house_panel::panel_ui {

struct ShellRenderModel {
	LifecycleMode lifecycle_mode;
	ShellTransitionState transition_state;
	std::string active_surface_id;
	std::vector<std::string> visible_text_runs;
};

struct HeadlessRenderFrame {
	std::uint64_t render_generation;
	LifecycleMode lifecycle_mode;
	ShellTransitionState transition_state;
	std::string active_surface_id;
	std::vector<std::string> visible_text_runs;
};

// @canonical panel_headless_renderer -- deterministic renderer evidence without device claims.
class HeadlessRenderer final {
public:
	[[nodiscard]] static PanelUiResult<std::unique_ptr<HeadlessRenderer>>
	create(UnicodeTextPolicyConfiguration unicode_configuration);

	[[nodiscard]] PanelUiResult<HeadlessRenderFrame>
	render(const ShellRenderModel &render_model);

private:
	explicit HeadlessRenderer(std::unique_ptr<BoundedUnicodeTextPolicy> unicode_policy);

	std::unique_ptr<BoundedUnicodeTextPolicy> unicode_policy_;
	std::uint64_t latest_render_generation_{0};
};

} // namespace house_panel::panel_ui
