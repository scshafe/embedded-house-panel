#pragma once

#include "panel_ui/headless_renderer.hpp"
#include "panel_ui/lifecycle_display_commit_backend.hpp"
#include "panel_ui/lifecycle_presentation_connection.hpp"
#include "panel_ui/monotonic_clock.hpp"
#include "panel_ui/panel_ui_service_control.hpp"
#include "panel_ui/result.hpp"
#include "panel_ui/shell_controller.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace house_panel::panel_ui {

struct PanelUiServiceConfiguration {
	std::chrono::milliseconds lifecycle_receive_timeout;
	std::chrono::milliseconds reconnect_delay;
	std::size_t maximum_consecutive_failures;
	std::string active_surface_id;
};

struct PanelUiServiceSummary {
	std::uint64_t accepted_connection_count;
	std::uint64_t presentation_attempt_count;
	std::uint64_t committed_presentation_count;
	std::uint64_t failed_presentation_count;
	std::uint64_t reconnect_failure_count;
};

using LifecyclePresentationConnectionFactory =
	std::function<PanelUiResult<std::unique_ptr<LifecyclePresentationConnection>>() >;
using PanelUiServiceErrorSink = std::function<void(const PanelUiError &error)>;

// @canonical panel_ui_service -- persistent neutral shell/lifecycle assembly with bounded
// reconnect behavior and display-commit-gated acknowledgements.
class PanelUiService final {
public:
	[[nodiscard]] static PanelUiResult<std::unique_ptr<PanelUiService>> create(
		PanelUiServiceConfiguration configuration,
		std::unique_ptr<ShellController> shell_controller,
		std::unique_ptr<HeadlessRenderer> headless_renderer,
		LifecycleDisplayCommitBackend &display_commit_backend,
		LifecyclePresentationConnectionFactory connection_factory,
		PanelUiServiceControl &service_control, MonotonicClock &monotonic_clock,
		PanelUiServiceErrorSink error_sink);

	/**
	 * Runs until stop is requested or the explicit consecutive-failure ceiling is
	 * reached. Every live connection processes snapshots until it fails; failures
	 * close the connection before the bounded reconnect wait.
	 */
	[[nodiscard]] PanelUiResult<PanelUiServiceSummary> run();

private:
	PanelUiService(PanelUiServiceConfiguration configuration,
		       std::unique_ptr<ShellController> shell_controller,
		       std::unique_ptr<HeadlessRenderer> headless_renderer,
		       LifecycleDisplayCommitBackend &display_commit_backend,
		       LifecyclePresentationConnectionFactory connection_factory,
		       PanelUiServiceControl &service_control, MonotonicClock &monotonic_clock,
		       PanelUiServiceErrorSink error_sink);

	[[nodiscard]] PanelUiResult<LifecycleRenderCommit>
	render_and_commit(const bhp::panel_mode_protocol::Snapshot &snapshot);
	[[nodiscard]] PanelUiResult<bool> report_error(const PanelUiError &error);
	[[nodiscard]] PanelUiResult<bool> wait_before_reconnect();
	[[nodiscard]] PanelUiResult<PanelUiServiceSummary>
	failure_limit_error(const PanelUiError &last_error) const;

	PanelUiServiceConfiguration configuration_;
	std::unique_ptr<ShellController> shell_controller_;
	std::unique_ptr<HeadlessRenderer> headless_renderer_;
	LifecycleDisplayCommitBackend &display_commit_backend_;
	LifecyclePresentationConnectionFactory connection_factory_;
	PanelUiServiceControl &service_control_;
	MonotonicClock &monotonic_clock_;
	PanelUiServiceErrorSink error_sink_;
	PanelUiServiceSummary summary_{0U, 0U, 0U, 0U, 0U};
	std::size_t consecutive_failure_count_{0U};
};

} // namespace house_panel::panel_ui
