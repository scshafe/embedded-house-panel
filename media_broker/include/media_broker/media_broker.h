#pragma once
#include "bhp/media_lease_protocol/media_lease_protocol.h"
#include "bhp/media_window_protocol/media_window_protocol.h"
#include "media_broker/capture_source.h"
#include "media_broker/take_store.h"
#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <thread>
namespace media_broker
{
struct BrokerSettings {
	CaptureRequest request;
	/** 600 s at the request's rate in production (plan Decision 3). */
	std::uint64_t maximum_frames{0};
	std::uint64_t tolerance_ns{0};
	std::chrono::milliseconds start_deadline{1000};
	std::chrono::milliseconds claim_deadline{2000};
	std::chrono::milliseconds level_interval{100};
};

struct BrokerPorts {
	std::function<std::unique_ptr<CaptureSource>()> make_source;
	/** Device, firmware and settings read at the start of each take (JSON null when none). */
	std::function<nlohmann::json()> read_device;
	/** RFC 3339 UTC wall time for the take record. */
	std::function<std::string()> utc_now;
	/** Unpredictable opaque IDs (lease and claim request IDs), 22..128 of [A-Za-z0-9_-]. */
	std::function<std::string()> new_id;
	std::function<std::chrono::steady_clock::time_point()> now;
};

/** "sha256:" + hex SHA-256 of the canonical operation a voice_take lease claims. */
std::string voice_take_operation_digest(const std::string &request_id, const std::string &operation_ref);

/**
 * @canonical media_broker -- the one audio owner and its voice_take leases.
 *
 * One UI channel and one supervisor window at a time; one lease at a time.
 * `arm` (voice_take only, window open, room in the store) claims the named
 * touch observation from the supervisor for a digest of the request; a
 * matching envelope arms a lease with an unpredictable ID. `start` must name
 * that lease before `start_deadline`; it reads the device identity, opens the
 * source and records on a capture thread. The take ends on `stop`
 * (requested), at `maximum_frames` (maximum-duration), or as a failure on a
 * closed window, a UI that leaves, an xrun or a device error. A failed take
 * leaves no files. `step()` never blocks on audio.
 */
class MediaBroker final
{
public:
	MediaBroker(BrokerSettings settings, TakeStore store, BrokerPorts ports);
	~MediaBroker();
	MediaBroker(const MediaBroker &) = delete;
	MediaBroker &operator=(const MediaBroker &) = delete;

	/** False (and the channel is closed) when a UI is already attached. */
	bool attach_ui(std::unique_ptr<bhp::media_window_protocol::FrameChannel> channel);
	void attach_window(std::unique_ptr<bhp::media_window_protocol::MediaWindowClient> window);
	bool ui_attached() const { return ui_ && ui_->open(); }
	bool window_attached() const { return window_ && window_->connected(); }
	/** Ends any take as a failure with `error_code` (the daemon is stopping). */
	void abort(const std::string &error_code);
	void step();

	enum class State { Idle, Claiming, Armed, Active };
	State state() const { return state_; }

private:
	struct CaptureEvent {
		std::optional<std::vector<double>> peaks;
		std::uint64_t elapsed_milliseconds{0};
		std::optional<MediaResult<TakeSummary>> finished;
	};

	void step_window();
	void step_ui();
	void step_capture();
	void step_deadlines();
	void handle(const bhp::media_lease_protocol::UiRequest &request);
	void arm(const bhp::media_lease_protocol::ArmRequest &request);
	void start(const std::string &lease_id);
	void stop(const std::string &lease_id);
	void send(const bhp::media_lease_protocol::BrokerEvent &event);
	void refuse(const std::string &request_id, const std::string &code);
	void end_failed(const std::string &code);
	void capture_loop(std::unique_ptr<CaptureSource> source, std::unique_ptr<TakeWriter> writer,
			  CaptureFormat format);
	void reset_lease();

	BrokerSettings settings_;
	TakeStore store_;
	BrokerPorts ports_;
	std::unique_ptr<bhp::media_window_protocol::FrameChannel> ui_;
	std::unique_ptr<bhp::media_window_protocol::MediaWindowClient> window_;
	State state_{State::Idle};
	std::string request_id_;
	std::string claim_id_;
	std::string touch_observation_id_;
	std::string digest_;
	std::string lease_id_;
	std::chrono::steady_clock::time_point deadline_{};

	std::thread capture_;
	std::atomic<int> stop_{0};
	std::mutex events_mutex_;
	std::deque<CaptureEvent> events_;
	std::string abort_code_;
};
} // namespace media_broker
