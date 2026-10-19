#pragma once
#include "panel_supervisor/result.hpp"
#include "panel_supervisor/touch_observation_authority.hpp"
#include "bhp/media_window_protocol/media_window_protocol.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace house_panel::supervisor {
/**
 * @canonical media_window_endpoint -- the supervisor's socket for the media broker.
 *
 * Accepts one peer whose SO_PEERCRED UID is exactly the broker UID; any other
 * peer is closed unread. On connect it sends the current window, then every
 * change, each with this supervisor session and the next sequence. It answers
 * `claim` frames by consuming the named touch observation for the media
 * broker audience; while the window is closed it refuses claims without
 * consuming anything. A malformed frame, a send that is not whole, or a peer
 * that leaves drops the connection; the broker then treats the window as
 * closed. Nothing here starts, stops or reads audio.
 */
class MediaWindowEndpoint final {
public:
	using Redeem = std::function<Result<TouchProvenanceEnvelope>(const TouchObservationClaim &)>;

	/**
	 * Root only: binds `path` under root-owned, non-writable parents, as root
	 * with the parent directory's group and mode 0660.
	 */
	[[nodiscard]] static Result<std::unique_ptr<MediaWindowEndpoint>> listen(
		std::string path, std::uint32_t broker_uid, std::string supervisor_session_id);
	/** Test seam: an already-connected stream socket; the peer UID is still checked. */
	[[nodiscard]] static Result<std::unique_ptr<MediaWindowEndpoint>> for_connected_peer(
		int descriptor, std::uint32_t broker_uid, std::string supervisor_session_id);
	~MediaWindowEndpoint();
	MediaWindowEndpoint(const MediaWindowEndpoint &) = delete;
	MediaWindowEndpoint &operator=(const MediaWindowEndpoint &) = delete;

	/** Records the window and sends it now when it changed and a broker is connected. */
	void set_window(bool open, bhp::media_window_protocol::WindowReason reason);
	/** Accepts the broker and answers at most four claims. Bounded, nonblocking. */
	[[nodiscard]] Result<void> pump(const Redeem &redeem);
	[[nodiscard]] bool connected() const noexcept;
	[[nodiscard]] bool window_open() const noexcept { return open_; }

private:
	MediaWindowEndpoint(std::string path, int listener, std::uint32_t broker_uid,
		std::string supervisor_session_id);
	[[nodiscard]] Result<void> adopt(int descriptor);
	[[nodiscard]] Result<void> send_window();
	[[nodiscard]] Result<void> answer(const std::string &frame, const Redeem &redeem);
	void drop() noexcept;
	std::string path_;
	int listener_;
	std::uint32_t broker_uid_;
	std::string session_;
	std::unique_ptr<bhp::media_window_protocol::FrameChannel> peer_;
	std::uint64_t sequence_{0};
	bool open_{false};
	bhp::media_window_protocol::WindowReason reason_{bhp::media_window_protocol::WindowReason::Absence};
};
}
