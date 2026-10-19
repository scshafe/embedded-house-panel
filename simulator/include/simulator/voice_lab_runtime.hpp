#pragma once
#include "bhp/media_lease_protocol/media_lease_protocol.h"
#include "bhp/media_window_protocol/media_window_protocol.h"
#include "media_broker/media_broker.h"
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace bhp::simulator
{
/**
 * @canonical simulator_voice_lab_runtime -- the recorder's path to the media broker.
 *
 * In-process mode (simulator and hosts) runs the real `MediaBroker` on the
 * paced fake source in a temporary take store, behind a stand-in supervisor
 * that publishes the window and grants each synthetic touch once. The
 * recorder talks to it only through `MediaLeaseClient`, as it will to the
 * `bhp-media-broker` process on the board.
 */
class VoiceLabRuntime final
{
public:
	struct Event {
		enum class Kind { Started, Level, Committed, Failed, Discarded };
		Kind kind{Kind::Failed};
		std::vector<int> level_tenths;
		std::uint64_t elapsed_ms{0};
		std::string take_id;
		std::uint64_t frames{0};
		std::string code;
	};

	/** Throws SIMULATOR.VOICE_LAB_UNAVAILABLE when the broker cannot be built. */
	static std::unique_ptr<VoiceLabRuntime> in_process(std::uint64_t maximum_seconds = 600);
	~VoiceLabRuntime();
	VoiceLabRuntime(const VoiceLabRuntime &) = delete;
	VoiceLabRuntime &operator=(const VoiceLabRuntime &) = delete;

	/** The stand-in supervisor's window: open only while the panel is day-active and present. */
	void set_window(bool open);
	/** Arms a lease for this prompt with a fresh synthetic touch; false while a take is in progress. */
	bool start_take(const std::string &operation_ref);
	void stop();
	void discard(const std::string &take_id);
	/** Ends any take as a failure (a scenario reload); its files are erased. */
	void abort();
	std::vector<Event> pump();
	bool busy() const { return pending_ != Pending::None; }
	const std::string &take_root() const { return take_root_; }

private:
	enum class Pending { None, Arming, Starting, Recording };
	VoiceLabRuntime() = default;
	void pump_supervisor();

	std::string temporary_root_;
	std::string take_root_;
	std::unique_ptr<media_broker::MediaBroker> broker_;
	std::unique_ptr<media_window_protocol::FrameChannel> supervisor_;
	std::unique_ptr<media_lease_protocol::MediaLeaseClient> client_;
	std::uint64_t supervisor_sequence_{0};
	bool window_open_{false};
	bool window_sent_{false};
	std::vector<std::string> granted_touches_;
	std::uint64_t counter_{0};
	Pending pending_{Pending::None};
	std::string request_id_;
	std::string lease_id_;
};
} // namespace bhp::simulator
