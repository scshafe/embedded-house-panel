#pragma once
#include "bhp/touch_observation_protocol/touch_observation_codec.h"
#include <memory>
#include <optional>

namespace bhp::touch_observation_protocol {
/** Linux-only observation server. Reverse traffic permits only connect/disconnect.
 * Every operation has a fixed syscall/byte budget; send failure drops the peer.
 * The producer MUST invalidate all authority evidence whenever pump/publish fails.
 */
class UnixTouchObservationServer final {
public:
	[[nodiscard]] static Result<std::unique_ptr<UnixTouchObservationServer>> create(
		std::string path, std::uint32_t expected_ui_uid);
	~UnixTouchObservationServer();
	UnixTouchObservationServer(const UnixTouchObservationServer &) = delete;
	UnixTouchObservationServer &operator=(const UnixTouchObservationServer &) = delete;
	/** Accepts at most four peers; authenticates exact UID and checks reverse silence. */
	[[nodiscard]] Result<void> pump();
	/** One nonblocking whole-frame send; partial write/EAGAIN disconnects immediately. */
	[[nodiscard]] Result<void> try_publish(const NormalizedTouchObservation &value);
	void disconnect() noexcept;
	[[nodiscard]] bool connected() const noexcept { return peer_ >= 0; }
private:
	UnixTouchObservationServer(std::string path, int listener, std::uint32_t uid,
		std::uint64_t device, std::uint64_t inode);
	std::string path_;
	int listener_;
	int peer_{-1};
	[[maybe_unused]] std::uint32_t uid_;
	std::uint64_t device_, inode_;
};

/** Linux-only receive-only client: exact SO_PEERCRED UID plus trusted root-owned path.
 * It never sends bytes, calibrates coordinates, or grants provenance.
 */
class UnixTouchObservationClient final {
public:
	[[nodiscard]] static Result<std::unique_ptr<UnixTouchObservationClient>> connect(
		const std::string &path, std::uint32_t expected_supervisor_uid);
	~UnixTouchObservationClient();
	UnixTouchObservationClient(const UnixTouchObservationClient &) = delete;
	UnixTouchObservationClient &operator=(const UnixTouchObservationClient &) = delete;
	/** One read bounded to 2048 bytes; a partial frame yields an empty optional. */
	[[nodiscard]] Result<std::optional<NormalizedTouchObservation>> try_receive();
	void disconnect() noexcept;
	[[nodiscard]] bool connected() const noexcept { return peer_ >= 0; }
private:
	explicit UnixTouchObservationClient(int peer) : peer_(peer) {}
	int peer_;
	std::string buffered_;
	std::uint64_t latest_sequence_{0};
};
}
