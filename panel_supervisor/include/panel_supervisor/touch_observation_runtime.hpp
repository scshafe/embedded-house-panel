#pragma once
#include "panel_supervisor/media_window_endpoint.hpp"
#include "panel_supervisor/touch_runtime_configuration.hpp"
#include "panel_supervisor/unix_socket_ui_mode_notifier.hpp"
#include "bhp/touch_observation_protocol/unix_touch_observation_socket.h"

namespace house_panel::supervisor {
/** Lifecycle-subordinate touch observer. No capability claims are exposed over the
 * receive-only observation socket. Claims arrive only through the optional media
 * window endpoint, from the exact broker UID, while the window is open.
 */
class TouchObservationRuntime final : public UiModeNotifier {
public:
	[[nodiscard]] static Result<std::unique_ptr<TouchObservationRuntime>> create(
		TouchRuntimeConfiguration configuration, std::uint32_t expected_ui_uid,
		UnixSocketUiModeNotifier &lifecycle,
		std::unique_ptr<MediaWindowEndpoint> media_window = nullptr);
	[[nodiscard]] Result<PendingPresentation> publish(const UiModeNotification &notification) override;
	[[nodiscard]] PresentationOutcome await_presentation(const PendingPresentation &pending,
		std::chrono::steady_clock::time_point deadline) override;
	/** Fixed bounded work. Caller logs failure and continues power policy unchanged. */
	[[nodiscard]] Result<void> poll_observations();
private:
	TouchObservationRuntime(UnixSocketUiModeNotifier &lifecycle,
		std::unique_ptr<EvdevTouchSource> source,
		std::unique_ptr<bhp::touch_observation_protocol::UnixTouchObservationServer> server,
		std::unique_ptr<TouchObservationAuthority> authority,
		std::unique_ptr<MediaWindowEndpoint> media_window);
	void invalidate(TouchObservationInvalidation reason) noexcept;
	[[nodiscard]] Result<void> observe(const RawTouchContact &contact);
	[[nodiscard]] Result<void> poll_touch();
	UnixSocketUiModeNotifier &lifecycle_;
	std::unique_ptr<EvdevTouchSource> source_;
	std::unique_ptr<bhp::touch_observation_protocol::UnixTouchObservationServer> server_;
	std::unique_ptr<TouchObservationAuthority> authority_;
	std::unique_ptr<MediaWindowEndpoint> media_window_;
	std::optional<TouchPresentationBinding> binding_;
	std::optional<SupervisorError> pending_error_;
	bool eligible_{false}, contact_active_{false}, source_failed_{false};
	std::string forwarding_id_;
	std::uint64_t sequence_{0};
};
}
