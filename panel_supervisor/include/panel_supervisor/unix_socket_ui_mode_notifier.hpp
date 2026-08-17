#pragma once

#include "panel_supervisor/ui_mode_notifier.hpp"

#include "bhp/panel_mode_protocol/types.h"
#include "bhp/panel_mode_protocol/framing.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace house_panel::supervisor {

// @canonical panel_mode_unix_socket_server -- authenticated one-client version-2 lifecycle and
// presentation-readiness boundary.
class UnixSocketUiModeNotifier final : public UiModeNotifier {
public:
	/**
	 * Creates, binds, chmods to 0660, and listens on an explicit Unix socket path.
	 *
	 * A pre-existing active, non-socket, or foreign-owner path is preserved. A
	 * same-effective-owner socket is recovered only when a connection probe proves
	 * it stale and a final type/owner/device/inode check still matches.
	 */
	[[nodiscard]] static Result<std::unique_ptr<UnixSocketUiModeNotifier>>
	create(std::string socket_path, std::uint32_t expected_panel_ui_uid,
	       std::string appliance_release_id);

	~UnixSocketUiModeNotifier() override;

	UnixSocketUiModeNotifier(const UnixSocketUiModeNotifier &) = delete;
	UnixSocketUiModeNotifier &operator=(const UnixSocketUiModeNotifier &) = delete;
	UnixSocketUiModeNotifier(UnixSocketUiModeNotifier &&) = delete;
	UnixSocketUiModeNotifier &operator=(UnixSocketUiModeNotifier &&) = delete;

	/**
	 * Accepts at most one exact-UID UI client and publishes a complete protocol-v2 snapshot.
	 *
	 * Extra clients are immediately closed without disturbing the primary client
	 * or panel policy. Per-client setup/send/protocol failure isolates that client
	 * and remains a successful publication. The server never consumes UI
	 * input and UI data cannot affect power policy.
	 */
	[[nodiscard]] Result<PendingPresentation>
	publish(const UiModeNotification &notification) override;

	/**
	 * Waits only until the explicit deadline for the one exact acknowledgement.
	 * Timeout, disconnect, stale, duplicate, mismatch, and explicit failure are
	 * degraded outcomes. Malformed protocol input closes the connection and never
	 * grants readiness.
	 */
	[[nodiscard]] PresentationOutcome await_presentation(
		const PendingPresentation &pending_presentation,
		std::chrono::steady_clock::time_point deadline) override;

	[[nodiscard]] const std::string &socket_path() const noexcept;
	[[nodiscard]] std::uint64_t event_sequence() const noexcept;
	[[nodiscard]] bool has_connected_client() const noexcept;

	/** Hard per-call accept syscall budget; connection floods cannot monopolize policy. */
	[[nodiscard]] static constexpr std::size_t
	maximum_accept_attempts_per_pass() noexcept
	{
		return 4U;
	}

private:
	UnixSocketUiModeNotifier(std::string socket_path, int listening_socket,
				 std::uint64_t socket_device,
				 std::uint64_t socket_inode,
				 std::uint32_t expected_panel_ui_uid,
				 std::string appliance_release_id,
				 std::string supervisor_session_id);

	[[nodiscard]] Result<bool> accept_pending_primary_client();
	[[nodiscard]] bool send_line_to_client(const std::string &json_line) noexcept;
	[[nodiscard]] Result<bool> send_latest_snapshot_to_client();
	void close_client() noexcept;
	void invalidate_pending() noexcept;
	[[nodiscard]] Result<bhp::panel_mode_protocol::Snapshot>
	make_snapshot(const UiModeNotification &notification, std::uint64_t sequence) const;

	std::string socket_path_;
	int listening_socket_;
	int client_socket_{-1};
	std::uint32_t expected_panel_ui_uid_;
	std::string appliance_release_id_;
	std::string supervisor_session_id_;
	std::string connection_challenge_;
	std::uint64_t event_sequence_{0};
	std::uint64_t last_render_generation_{0};
	std::optional<UiModeNotification> latest_notification_;
	std::optional<bhp::panel_mode_protocol::Snapshot> pending_snapshot_;
	bhp::panel_mode_protocol::FrameAccumulator acknowledgement_frames_;
	std::uint64_t socket_device_;
	std::uint64_t socket_inode_;
};

} // namespace house_panel::supervisor
