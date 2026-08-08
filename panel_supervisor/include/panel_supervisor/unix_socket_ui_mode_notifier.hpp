#pragma once

#include "panel_supervisor/ui_mode_notifier.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace house_panel::supervisor {

// @canonical panel_mode_unix_socket_server -- one-client version-1 JSON Lines publisher.
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
	create(std::string socket_path);

	~UnixSocketUiModeNotifier() override;

	UnixSocketUiModeNotifier(const UnixSocketUiModeNotifier &) = delete;
	UnixSocketUiModeNotifier &operator=(const UnixSocketUiModeNotifier &) = delete;
	UnixSocketUiModeNotifier(UnixSocketUiModeNotifier &&) = delete;
	UnixSocketUiModeNotifier &operator=(UnixSocketUiModeNotifier &&) = delete;

	/**
	 * Accepts at most one UI client and publishes a complete protocol-v1 snapshot.
	 *
	 * Extra clients are immediately closed without disturbing the primary client
	 * or panel policy. Per-client setup/send/protocol failure isolates that client
	 * and remains a successful publication. The server never consumes UI
	 * input and UI data cannot affect power policy.
	 */
	[[nodiscard]] Result<PendingPresentation>
	publish(const UiModeNotification &notification) override;

	/**
	 * Returns immediate non-commit because protocol v1 has no acknowledgement.
	 *
	 * The deadline and pending sequence are accepted to preserve the presentation
	 * seam, but this adapter never fabricates renderer commitment from a socket
	 * write or connected client.
	 */
	[[nodiscard]] PresentationOutcome await_presentation(
		const PendingPresentation &pending_presentation,
		std::chrono::steady_clock::time_point deadline) override;

	[[nodiscard]] const std::string &socket_path() const noexcept;
	[[nodiscard]] std::uint64_t event_sequence() const noexcept;
	[[nodiscard]] bool has_connected_client() const noexcept;

private:
	UnixSocketUiModeNotifier(std::string socket_path, int listening_socket,
				 std::uint64_t socket_device,
				 std::uint64_t socket_inode);

	[[nodiscard]] Result<bool> accept_pending_primary_client();
	[[nodiscard]] bool send_line_to_client(const std::string &json_line) noexcept;
	void close_client() noexcept;
	[[nodiscard]] Result<std::string>
	encode_notification(const UiModeNotification &notification, std::uint64_t sequence) const;

	std::string socket_path_;
	int listening_socket_;
	int client_socket_{-1};
	std::uint64_t event_sequence_{0};
	std::string current_snapshot_line_;
	std::uint64_t socket_device_;
	std::uint64_t socket_inode_;
};

} // namespace house_panel::supervisor
