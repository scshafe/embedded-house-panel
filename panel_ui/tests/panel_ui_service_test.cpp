#include "panel_ui/fail_dark_lifecycle_display_commit_backend.hpp"
#include "panel_ui/panel_ui_service.hpp"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace house_panel::panel_ui {
namespace {

using namespace std::chrono_literals;
namespace protocol = bhp::panel_mode_protocol;

#define TEST_REQUIRE(condition)                                                                    \
	do {                                                                                       \
		if (!(condition)) {                                                                \
			std::cerr << __func__ << ": requirement failed: " #condition << '\n';      \
			return false;                                                              \
		}                                                                                  \
	} while (false)

class FixtureMonotonicClock final : public MonotonicClock {
public:
	[[nodiscard]] std::chrono::steady_clock::time_point now() const noexcept override
	{
		return current_time_;
	}

private:
	std::chrono::steady_clock::time_point current_time_{
		std::chrono::steady_clock::time_point{1000ms}};
};

class FixtureServiceControl final : public PanelUiServiceControl {
public:
	[[nodiscard]] bool stop_requested() const noexcept override { return stop_requested_; }

	[[nodiscard]] PanelUiResult<bool>
	wait_for(const std::chrono::milliseconds delay) override
	{
		waited_delays_.push_back(delay);
		return PanelUiResult<bool>::success(!stop_requested_);
	}

	void request_stop() noexcept { stop_requested_ = true; }

	[[nodiscard]] const std::vector<std::chrono::milliseconds> &waited_delays() const noexcept
	{
		return waited_delays_;
	}

private:
	bool stop_requested_{false};
	std::vector<std::chrono::milliseconds> waited_delays_;
};

struct FixtureConnectionState {
	std::vector<std::string> events;
	std::vector<LifecycleRenderCommit> acknowledged_commits;
	bool closed{false};
};

class FixtureLifecycleConnection final : public LifecyclePresentationConnection {
public:
	FixtureLifecycleConnection(std::vector<protocol::Snapshot> snapshots,
				   FixtureServiceControl &control,
				   std::shared_ptr<FixtureConnectionState> state,
				   const std::size_t stop_after_presentation_count)
		: snapshots_(std::move(snapshots)), control_(control), state_(std::move(state)),
		  stop_after_presentation_count_(stop_after_presentation_count)
	{
	}

	[[nodiscard]] PanelUiResult<std::optional<protocol::Snapshot>>
	receive_until(std::chrono::steady_clock::time_point deadline) override
	{
		(void)deadline;
		if (next_snapshot_index_ >= snapshots_.size()) {
			return PanelUiResult<std::optional<protocol::Snapshot>>::success(
				std::nullopt);
		}
		return PanelUiResult<std::optional<protocol::Snapshot>>::success(
			snapshots_[next_snapshot_index_++]);
	}

	[[nodiscard]] PanelUiResult<LifecycleRenderCommit>
	receive_render_and_acknowledge_until(
		std::chrono::steady_clock::time_point deadline,
		const LifecycleRenderCommitCallback &render_and_commit) override
	{
		(void)deadline;
		if (!connected_ || next_snapshot_index_ >= snapshots_.size()) {
			connected_ = false;
			return PanelUiResult<LifecycleRenderCommit>::failure(PanelUiError(
				PanelUiErrorCode::LifecycleSocketDisconnected,
				"Fixture lifecycle connection has no next snapshot",
				{{"operation", "fixture_lifecycle_connection.receive"}}));
		}
		state_->events.emplace_back("render_callback_started");
		auto result = render_and_commit(snapshots_[next_snapshot_index_++]);
		if (!result) {
			connected_ = false;
			return result;
		}
		state_->events.emplace_back("acknowledgement_sent");
		state_->acknowledged_commits.push_back(result.value());
		if (state_->acknowledged_commits.size() == stop_after_presentation_count_) {
			control_.request_stop();
		}
		return result;
	}

	void close() noexcept override
	{
		connected_ = false;
		state_->closed = true;
	}

	[[nodiscard]] bool connected() const noexcept override { return connected_; }

private:
	std::vector<protocol::Snapshot> snapshots_;
	FixtureServiceControl &control_;
	std::shared_ptr<FixtureConnectionState> state_;
	std::size_t stop_after_presentation_count_;
	std::size_t next_snapshot_index_{0U};
	bool connected_{true};
};

class FixtureDisplayCommitBackend final : public LifecycleDisplayCommitBackend {
public:
	FixtureDisplayCommitBackend(protocol::PresentationResult presentation_result,
				    std::vector<std::string> &events,
				    const bool return_wrong_generation = false)
		: presentation_result_(presentation_result), events_(events),
		  return_wrong_generation_(return_wrong_generation)
	{
	}

	[[nodiscard]] PanelUiResult<LifecycleRenderCommit>
	commit(const protocol::Snapshot &snapshot,
	       const HeadlessRenderFrame &validated_frame) override
	{
		(void)snapshot;
		events_.emplace_back("display_backend_commit_completed");
		observed_frames_.push_back(validated_frame);
		return PanelUiResult<LifecycleRenderCommit>::success(LifecycleRenderCommit{
			validated_frame.render_generation + (return_wrong_generation_ ? 1U : 0U),
			presentation_result_});
	}

	[[nodiscard]] const std::vector<HeadlessRenderFrame> &observed_frames() const noexcept
	{
		return observed_frames_;
	}

private:
	protocol::PresentationResult presentation_result_;
	std::vector<std::string> &events_;
	bool return_wrong_generation_;
	std::vector<HeadlessRenderFrame> observed_frames_;
};

protocol::Snapshot fixture_snapshot(const std::uint64_t event_sequence,
				    const protocol::DisplayMode display_mode =
					    protocol::DisplayMode::DayActive)
{
	return protocol::Snapshot{
		"fixture-release",
		"00112233445566778899aabbccddeeff",
		"ffeeddccbbaa99887766554433221100",
		event_sequence,
		"sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
		display_mode,
		protocol::PresenceState::Present,
		protocol::SolarState::Day,
		protocol::SystemState::Active,
		0U,
		1'700'000'000'000LL + static_cast<std::int64_t>(event_sequence),
	};
}

PanelUiResult<std::unique_ptr<ShellController>>
fixture_shell_controller(FixtureMonotonicClock &clock)
{
	return ShellController::create(
		ShellControllerConfiguration{10ms, "fixture-conflict-window-evidence", 8U,
					     false},
		std::nullopt,
		{SiblingSurfaceContract{"neutral_surface", {"study.flip", "study.next"}}},
		clock);
}

PanelUiResult<std::unique_ptr<PanelUiService>> fixture_service(
	FixtureMonotonicClock &clock, FixtureServiceControl &control,
	LifecycleDisplayCommitBackend &backend,
	LifecyclePresentationConnectionFactory connection_factory,
	std::vector<PanelUiErrorCode> &reported_errors,
	const std::size_t maximum_consecutive_failures = 3U)
{
	auto controller = fixture_shell_controller(clock);
	if (!controller) {
		return PanelUiResult<std::unique_ptr<PanelUiService>>::failure(controller.error());
	}
	auto renderer = HeadlessRenderer::create({64U, 32U, false});
	if (!renderer) {
		return PanelUiResult<std::unique_ptr<PanelUiService>>::failure(renderer.error());
	}
	return PanelUiService::create(
		PanelUiServiceConfiguration{50ms, 7ms, maximum_consecutive_failures,
					    "neutral_surface"},
		std::move(controller).value(), std::move(renderer).value(), backend,
		std::move(connection_factory), control, clock,
		[&reported_errors](const PanelUiError &error) {
			reported_errors.push_back(error.code());
		});
}

bool committed_acknowledgement_follows_exact_backend_commit()
{
	FixtureMonotonicClock clock;
	FixtureServiceControl control;
	auto connection_state = std::make_shared<FixtureConnectionState>();
	FixtureDisplayCommitBackend backend(protocol::PresentationResult::Committed,
					    connection_state->events);
	std::vector<PanelUiErrorCode> errors;
	LifecyclePresentationConnectionFactory factory = [&]() {
		std::unique_ptr<LifecyclePresentationConnection> connection =
			std::make_unique<FixtureLifecycleConnection>(
				std::vector<protocol::Snapshot>{fixture_snapshot(1U),
							       fixture_snapshot(2U)},
				control, connection_state, 2U);
		return PanelUiResult<std::unique_ptr<LifecyclePresentationConnection>>::success(
			std::move(connection));
	};
	auto service = fixture_service(clock, control, backend, std::move(factory), errors);
	TEST_REQUIRE(service);
	const auto result = service.value()->run();
	TEST_REQUIRE(result);
	TEST_REQUIRE(result.value().accepted_connection_count == 1U);
	TEST_REQUIRE(result.value().presentation_attempt_count == 2U);
	TEST_REQUIRE(result.value().committed_presentation_count == 2U);
	TEST_REQUIRE(result.value().failed_presentation_count == 0U);
	TEST_REQUIRE(errors.empty());
	TEST_REQUIRE(connection_state->closed);
	TEST_REQUIRE(connection_state->events ==
		     std::vector<std::string>({"render_callback_started",
					      "display_backend_commit_completed",
					      "acknowledgement_sent",
					      "render_callback_started",
					      "display_backend_commit_completed",
					      "acknowledgement_sent"}));
	TEST_REQUIRE(backend.observed_frames().size() == 2U);
	TEST_REQUIRE(backend.observed_frames()[0].render_generation == 1U);
	TEST_REQUIRE(backend.observed_frames()[1].render_generation == 2U);
	TEST_REQUIRE(backend.observed_frames()[0].active_surface_id == "neutral_surface");
	return true;
}

bool fail_dark_backend_acknowledges_only_explicit_failure()
{
	FixtureMonotonicClock clock;
	FixtureServiceControl control;
	auto connection_state = std::make_shared<FixtureConnectionState>();
	FailDarkLifecycleDisplayCommitBackend backend;
	std::vector<PanelUiErrorCode> errors;
	LifecyclePresentationConnectionFactory factory = [&]() {
		std::unique_ptr<LifecyclePresentationConnection> connection =
			std::make_unique<FixtureLifecycleConnection>(
				std::vector<protocol::Snapshot>{fixture_snapshot(1U)}, control,
				connection_state, 1U);
		return PanelUiResult<std::unique_ptr<LifecyclePresentationConnection>>::success(
			std::move(connection));
	};
	auto service = fixture_service(clock, control, backend, std::move(factory), errors);
	TEST_REQUIRE(service);
	const auto result = service.value()->run();
	TEST_REQUIRE(result);
	TEST_REQUIRE(result.value().presentation_attempt_count == 1U);
	TEST_REQUIRE(result.value().committed_presentation_count == 0U);
	TEST_REQUIRE(result.value().failed_presentation_count == 1U);
	TEST_REQUIRE(connection_state->acknowledged_commits.size() == 1U);
	TEST_REQUIRE(connection_state->acknowledged_commits[0].presentation_result ==
		     protocol::PresentationResult::Failed);
	return true;
}

bool connection_failure_waits_then_reconnects_without_exiting_service()
{
	FixtureMonotonicClock clock;
	FixtureServiceControl control;
	auto connection_state = std::make_shared<FixtureConnectionState>();
	FixtureDisplayCommitBackend backend(protocol::PresentationResult::Committed,
					    connection_state->events);
	std::vector<PanelUiErrorCode> errors;
	std::size_t factory_call_count = 0U;
	LifecyclePresentationConnectionFactory factory = [&]() {
		++factory_call_count;
		if (factory_call_count == 1U) {
			return PanelUiResult<
				std::unique_ptr<LifecyclePresentationConnection>>::failure(
				PanelUiError(PanelUiErrorCode::LifecycleSocketConnectFailed,
					     "Fixture connection is unavailable",
					     {{"operation", "fixture_connection_factory"}}));
		}
		std::unique_ptr<LifecyclePresentationConnection> connection =
			std::make_unique<FixtureLifecycleConnection>(
				std::vector<protocol::Snapshot>{fixture_snapshot(1U)}, control,
				connection_state, 1U);
		return PanelUiResult<std::unique_ptr<LifecyclePresentationConnection>>::success(
			std::move(connection));
	};
	auto service = fixture_service(clock, control, backend, std::move(factory), errors);
	TEST_REQUIRE(service);
	const auto result = service.value()->run();
	TEST_REQUIRE(result);
	TEST_REQUIRE(factory_call_count == 2U);
	TEST_REQUIRE(control.waited_delays() ==
		     std::vector<std::chrono::milliseconds>{7ms});
	TEST_REQUIRE(errors ==
		     std::vector<PanelUiErrorCode>{PanelUiErrorCode::LifecycleSocketConnectFailed});
	TEST_REQUIRE(result.value().reconnect_failure_count == 1U);
	TEST_REQUIRE(result.value().presentation_attempt_count == 1U);
	return true;
}

bool failure_ceiling_is_structured_and_does_not_loop_forever()
{
	FixtureMonotonicClock clock;
	FixtureServiceControl control;
	std::vector<std::string> backend_events;
	FixtureDisplayCommitBackend backend(protocol::PresentationResult::Committed,
					    backend_events);
	std::vector<PanelUiErrorCode> errors;
	std::size_t factory_call_count = 0U;
	LifecyclePresentationConnectionFactory factory = [&]() {
		++factory_call_count;
		return PanelUiResult<std::unique_ptr<LifecyclePresentationConnection>>::failure(
			PanelUiError(PanelUiErrorCode::LifecycleSocketConnectFailed,
				     "Fixture connection remains unavailable",
				     {{"operation", "fixture_connection_factory"}}));
	};
	auto service = fixture_service(clock, control, backend, std::move(factory), errors, 2U);
	TEST_REQUIRE(service);
	const auto result = service.value()->run();
	TEST_REQUIRE(!result);
	TEST_REQUIRE(result.error().code() ==
		     PanelUiErrorCode::LifecycleServiceFailureLimitReached);
	TEST_REQUIRE(result.error().context().at("failure_count") == "2");
	TEST_REQUIRE(factory_call_count == 2U);
	TEST_REQUIRE(control.waited_delays().size() == 1U);
	TEST_REQUIRE(errors.size() == 2U);
	return true;
}

bool mismatched_commit_evidence_closes_without_acknowledgement()
{
	FixtureMonotonicClock clock;
	FixtureServiceControl control;
	auto connection_state = std::make_shared<FixtureConnectionState>();
	FixtureDisplayCommitBackend backend(protocol::PresentationResult::Committed,
					    connection_state->events, true);
	std::vector<PanelUiErrorCode> errors;
	LifecyclePresentationConnectionFactory factory = [&]() {
		std::unique_ptr<LifecyclePresentationConnection> connection =
			std::make_unique<FixtureLifecycleConnection>(
				std::vector<protocol::Snapshot>{fixture_snapshot(1U)}, control,
				connection_state, 1U);
		return PanelUiResult<std::unique_ptr<LifecyclePresentationConnection>>::success(
			std::move(connection));
	};
	auto service = fixture_service(clock, control, backend, std::move(factory), errors, 1U);
	TEST_REQUIRE(service);
	const auto result = service.value()->run();
	TEST_REQUIRE(!result);
	TEST_REQUIRE(result.error().code() ==
		     PanelUiErrorCode::LifecycleServiceFailureLimitReached);
	TEST_REQUIRE(errors == std::vector<PanelUiErrorCode>{
				       PanelUiErrorCode::LifecycleDisplayCommitEvidenceInvalid});
	TEST_REQUIRE(connection_state->acknowledged_commits.empty());
	TEST_REQUIRE(connection_state->closed);
	return true;
}

bool preexisting_stop_exits_without_connecting()
{
	FixtureMonotonicClock clock;
	FixtureServiceControl control;
	control.request_stop();
	std::vector<std::string> backend_events;
	FixtureDisplayCommitBackend backend(protocol::PresentationResult::Committed,
					    backend_events);
	std::vector<PanelUiErrorCode> errors;
	std::size_t factory_call_count = 0U;
	LifecyclePresentationConnectionFactory factory = [&]() {
		++factory_call_count;
		return PanelUiResult<std::unique_ptr<LifecyclePresentationConnection>>::failure(
			PanelUiError(PanelUiErrorCode::LifecycleSocketConnectFailed,
				     "Factory must not be called after stop",
				     {{"operation", "fixture_connection_factory"}}));
	};
	auto service = fixture_service(clock, control, backend, std::move(factory), errors);
	TEST_REQUIRE(service);
	const auto result = service.value()->run();
	TEST_REQUIRE(result);
	TEST_REQUIRE(factory_call_count == 0U);
	TEST_REQUIRE(result.value().accepted_connection_count == 0U);
	TEST_REQUIRE(result.value().presentation_attempt_count == 0U);
	return true;
}

} // namespace
} // namespace house_panel::panel_ui

int main()
{
	using Test = bool (*)();
	const std::vector<Test> tests{
		house_panel::panel_ui::committed_acknowledgement_follows_exact_backend_commit,
		house_panel::panel_ui::fail_dark_backend_acknowledges_only_explicit_failure,
		house_panel::panel_ui::connection_failure_waits_then_reconnects_without_exiting_service,
		house_panel::panel_ui::failure_ceiling_is_structured_and_does_not_loop_forever,
		house_panel::panel_ui::mismatched_commit_evidence_closes_without_acknowledgement,
		house_panel::panel_ui::preexisting_stop_exits_without_connecting,
	};
	std::size_t passed = 0U;
	for (const auto test : tests) {
		if (!test()) {
			return 1;
		}
		++passed;
	}
	std::cout << "panel_ui_service_test: " << passed << " passed\n";
	return 0;
}
