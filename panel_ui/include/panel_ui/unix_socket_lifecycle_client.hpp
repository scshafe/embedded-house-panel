#pragma once

#include "panel_ui/lifecycle_presentation_connection.hpp"
#include "panel_ui/result.hpp"

#include "bhp/panel_mode_protocol/framing.h"
#include "bhp/panel_mode_protocol/types.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace house_panel::panel_ui {

// @canonical panel_mode_unix_socket_client -- authenticated protocol-v2 lifecycle input and
// acknowledgement-only presentation output.
class UnixSocketLifecycleClient final : public LifecyclePresentationConnection {
public:
	/**
	 * Connects to an explicit Unix stream socket and verifies the exact supervisor UID.
	 *
	 * The socket path must be absolute, placeholder-free, and fit `sockaddr_un`.
	 * Failure returns a structured lifecycle socket/configuration error and closes the
	 * descriptor. The peer UID comes only from kernel credentials.
	 */
	[[nodiscard]] static PanelUiResult<std::unique_ptr<UnixSocketLifecycleClient>>
	connect(std::string socket_path, std::uint32_t expected_supervisor_uid);

	~UnixSocketLifecycleClient() override;

	UnixSocketLifecycleClient(const UnixSocketLifecycleClient &) = delete;
	UnixSocketLifecycleClient &operator=(const UnixSocketLifecycleClient &) = delete;
	UnixSocketLifecycleClient(UnixSocketLifecycleClient &&) = delete;
	UnixSocketLifecycleClient &operator=(UnixSocketLifecycleClient &&) = delete;

	/**
	 * Receives and validates exactly one typed snapshot before the finite deadline.
	 *
	 * The first snapshot must carry this binary's built-in release identity. Later
	 * snapshots must retain the connection session/challenge and strictly increase the
	 * event sequence. Any timeout, EOF, malformed frame, pipelined frame, digest failure,
	 * or binding failure closes the connection and returns a content-free typed error.
	 */
	[[nodiscard]] PanelUiResult<
		std::optional<bhp::panel_mode_protocol::Snapshot>>
	receive_until(std::chrono::steady_clock::time_point deadline) override;

	/**
	 * Calls the renderer for the exact pending snapshot and sends only its acknowledgement.
	 *
	 * `committed` is sent only after the callback explicitly reports a completed commit.
	 * `failed` is sent for an explicit attempted-generation failure. Generations must be
	 * positive safe JSON integers and strictly increase within this connection.
	 */
	[[nodiscard]] PanelUiResult<LifecycleRenderCommit>
	render_and_acknowledge_pending(const LifecycleRenderCommitCallback &render_and_commit);

	/** Receives, renders, and acknowledges one snapshot without exposing a substitution seam. */
	[[nodiscard]] PanelUiResult<LifecycleRenderCommit> receive_render_and_acknowledge_until(
		std::chrono::steady_clock::time_point deadline,
		const LifecycleRenderCommitCallback &render_and_commit) override;

	/** Closes the connection and invalidates every connection-local binding. */
	void close() noexcept override;

	[[nodiscard]] bool connected() const noexcept override;
	[[nodiscard]] const std::string &socket_path() const noexcept;
	[[nodiscard]] std::uint64_t last_render_generation() const noexcept;

private:
	UnixSocketLifecycleClient(std::string socket_path, int socket_descriptor);

	[[nodiscard]] PanelUiResult<bhp::panel_mode_protocol::Snapshot>
	receive_validated_snapshot_until(std::chrono::steady_clock::time_point deadline);
	[[nodiscard]] PanelUiResult<bool> validate_snapshot_binding(
		const bhp::panel_mode_protocol::Snapshot &snapshot);
	[[nodiscard]] PanelUiResult<bool> send_acknowledgement(
		const bhp::panel_mode_protocol::Snapshot &snapshot,
		const LifecycleRenderCommit &render_commit);
	[[nodiscard]] PanelUiResult<bool> fail_and_close(PanelUiError error);

	std::string socket_path_;
	int socket_descriptor_{-1};
	bhp::panel_mode_protocol::FrameAccumulator snapshot_frames_;
	std::string supervisor_session_id_;
	std::string connection_challenge_;
	std::uint64_t last_event_sequence_{0};
	std::uint64_t last_render_generation_{0};
	std::optional<bhp::panel_mode_protocol::Snapshot> pending_snapshot_;
};

} // namespace house_panel::panel_ui
