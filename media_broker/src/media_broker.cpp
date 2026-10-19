#include "media_broker/media_broker.h"
#include <algorithm>
#include <cmath>
#include <openssl/evp.h>
namespace media_broker
{
namespace lease = bhp::media_lease_protocol;
namespace window = bhp::media_window_protocol;
namespace
{
constexpr int kStopNone = 0;
constexpr int kStopRequested = 1;
constexpr int kStopAbort = 2;

std::string hex_sha256(const std::string &data)
{
	unsigned char digest[EVP_MAX_MD_SIZE];
	unsigned int size = 0;
	if (EVP_Digest(data.data(), data.size(), digest, &size, EVP_sha256(), nullptr) != 1 || size != 32)
		return {};
	std::string text;
	for (unsigned at = 0; at < size; ++at) {
		text += "0123456789abcdef"[digest[at] >> 4];
		text += "0123456789abcdef"[digest[at] & 15];
	}
	return text;
}
} // namespace

std::string voice_take_operation_digest(const std::string &request_id, const std::string &operation_ref)
{
	const nlohmann::json operation = {
		{"operation_ref", operation_ref}, {"purpose", "voice_take"}, {"request_id", request_id}};
	return "sha256:" + hex_sha256(operation.dump() + "\n");
}

MediaBroker::MediaBroker(BrokerSettings settings, TakeStore store, BrokerPorts ports)
	: settings_(std::move(settings)), store_(std::move(store)), ports_(std::move(ports))
{
}

MediaBroker::~MediaBroker()
{
	stop_ = kStopAbort;
	if (capture_.joinable())
		capture_.join();
}

bool MediaBroker::attach_ui(std::unique_ptr<window::FrameChannel> channel)
{
	if (ui_attached()) {
		channel->close();
		return false;
	}
	ui_ = std::move(channel);
	return true;
}

void MediaBroker::attach_window(std::unique_ptr<window::MediaWindowClient> client) { window_ = std::move(client); }

void MediaBroker::send(const lease::BrokerEvent &event)
{
	if (!ui_attached())
		return;
	auto frame = lease::encode(event);
	if (frame)
		(void)ui_->send(frame.value());
}

void MediaBroker::refuse(const std::string &request_id, const std::string &code)
{
	send(lease::Refused{request_id, code});
}

void MediaBroker::reset_lease()
{
	state_ = State::Idle;
	request_id_.clear();
	claim_id_.clear();
	touch_observation_id_.clear();
	digest_.clear();
	lease_id_.clear();
}

void MediaBroker::end_failed(const std::string &code)
{
	if (state_ == State::Active) {
		abort_code_ = code;
		stop_ = kStopAbort;
		return; // step_capture sends `ended` when the thread finishes.
	}
	if (state_ == State::Claiming)
		refuse(request_id_, code);
	else if (state_ == State::Armed)
		refuse(lease_id_, code);
	reset_lease();
}

void MediaBroker::abort(const std::string &error_code)
{
	end_failed(error_code);
	if (capture_.joinable()) {
		capture_.join();
		step_capture();
	}
}

void MediaBroker::step()
{
	step_window();
	step_ui();
	step_capture();
	step_deadlines();
}

void MediaBroker::step_window()
{
	if (!window_)
		return;
	auto update = window_->poll();
	if (!update) {
		window_.reset();
		if (state_ != State::Idle)
			end_failed("MEDIA_BROKER.WINDOW_CLOSED");
		return;
	}
	if (!window_->window_open() && state_ != State::Idle) {
		end_failed("MEDIA_BROKER.WINDOW_CLOSED");
		return;
	}
	for (const auto &result : update.value().claim_results) {
		if (state_ != State::Claiming || result.request_id != claim_id_)
			continue;
		if (!result.envelope) {
			refuse(request_id_, "MEDIA_BROKER.CLAIM_REFUSED." + result.error_code);
			reset_lease();
			continue;
		}
		if (result.envelope->touch_observation_id != touch_observation_id_ ||
		    result.envelope->canonical_operation_digest != digest_) {
			refuse(request_id_, "MEDIA_BROKER.LEASE_BINDING_MISMATCH");
			reset_lease();
			continue;
		}
		lease_id_ = ports_.new_id();
		state_ = State::Armed;
		deadline_ = ports_.now() + settings_.start_deadline;
		send(lease::Armed{request_id_, lease_id_});
	}
}

void MediaBroker::step_ui()
{
	for (int frames = 0; frames < 4 && ui_; ++frames) {
		auto received = ui_->receive();
		if (!received) {
			ui_.reset();
			if (state_ != State::Idle)
				end_failed("MEDIA_BROKER.UI_DISCONNECTED");
			return;
		}
		if (!received.value())
			return;
		auto request = lease::decode_ui_request(*received.value());
		if (!request) {
			// A malformed request ends the UI connection and everything it held.
			ui_.reset();
			if (state_ != State::Idle)
				end_failed("MEDIA_BROKER.REQUEST_INVALID");
			return;
		}
		handle(request.value());
	}
}

void MediaBroker::handle(const lease::UiRequest &request)
{
	if (const auto *arm_request = std::get_if<lease::ArmRequest>(&request))
		arm(*arm_request);
	else if (const auto *start_request = std::get_if<lease::StartRequest>(&request))
		start(start_request->lease_id);
	else
		stop(std::get<lease::StopRequest>(request).lease_id);
}

void MediaBroker::arm(const lease::ArmRequest &request)
{
	if (request.purpose != lease::Purpose::VoiceTake)
		return refuse(request.request_id, "MEDIA_BROKER.PURPOSE_DISABLED");
	if (state_ != State::Idle)
		return refuse(request.request_id, "MEDIA_BROKER.BUSY");
	if (!window_ || !window_->window_open())
		return refuse(request.request_id, "MEDIA_BROKER.WINDOW_CLOSED");
	const auto planned = 44 + settings_.maximum_frames * settings_.request.channels *
					  sample_width(settings_.request.format);
	if (auto admitted = store_.admit(planned); !admitted)
		return refuse(request.request_id, admitted.error().code);
	request_id_ = request.request_id;
	claim_id_ = ports_.new_id();
	touch_observation_id_ = request.touch_observation_id;
	digest_ = voice_take_operation_digest(request.request_id, request.operation_ref);
	if (!window_->claim({claim_id_, touch_observation_id_, digest_, request.presentation_binding})) {
		refuse(request.request_id, "MEDIA_BROKER.WINDOW_CLOSED");
		reset_lease();
		return;
	}
	state_ = State::Claiming;
	deadline_ = ports_.now() + settings_.claim_deadline;
}

void MediaBroker::start(const std::string &lease_id)
{
	if (state_ == State::Active && lease_id == lease_id_)
		return refuse(lease_id, "MEDIA_BROKER.START_ALREADY_CONSUMED");
	if (state_ != State::Armed || lease_id != lease_id_)
		return refuse(lease_id, "MEDIA_BROKER.LEASE_UNKNOWN");
	if (ports_.now() >= deadline_) {
		refuse(lease_id, "MEDIA_BROKER.LEASE_EXPIRED");
		return reset_lease();
	}
	auto source = ports_.make_source();
	auto format = source->open(settings_.request);
	if (!format) {
		send(lease::Ended{lease_id_, lease::Outcome::Failed, {}, {}, 0, format.error().code});
		return reset_lease();
	}
	auto writer = TakeWriter::begin(store_, format.value(),
					{ports_.utc_now(), settings_.tolerance_ns, settings_.maximum_frames,
					 ports_.read_device()});
	if (!writer) {
		source->close();
		send(lease::Ended{lease_id_, lease::Outcome::Failed, {}, {}, 0, writer.error().code});
		return reset_lease();
	}
	stop_ = kStopNone;
	abort_code_.clear();
	state_ = State::Active;
	capture_ = std::thread(&MediaBroker::capture_loop, this, std::move(source), std::move(writer.value()),
			       format.value());
	send(lease::Started{lease_id_});
}

void MediaBroker::stop(const std::string &lease_id)
{
	if (state_ != State::Active || lease_id != lease_id_)
		return refuse(lease_id, state_ == State::Armed && lease_id == lease_id_ ? "MEDIA_BROKER.NO_ACTIVE_CAPTURE"
											    : "MEDIA_BROKER.LEASE_UNKNOWN");
	int expected = kStopNone;
	stop_.compare_exchange_strong(expected, kStopRequested);
}

void MediaBroker::step_deadlines()
{
	if ((state_ == State::Claiming || state_ == State::Armed) && ports_.now() >= deadline_)
		end_failed(state_ == State::Claiming ? "MEDIA_BROKER.CLAIM_TIMEOUT" : "MEDIA_BROKER.LEASE_EXPIRED");
}

void MediaBroker::step_capture()
{
	std::deque<CaptureEvent> events;
	{
		std::lock_guard<std::mutex> lock(events_mutex_);
		events.swap(events_);
	}
	for (auto &event : events) {
		if (event.peaks && state_ == State::Active) {
			std::vector<std::int32_t> tenths;
			for (const double peak : *event.peaks)
				tenths.push_back(static_cast<std::int32_t>(std::clamp(std::lround(peak * 10.0), -1200L, 0L)));
			send(lease::Level{lease_id_, event.elapsed_milliseconds, tenths});
		}
		if (!event.finished)
			continue;
		if (capture_.joinable())
			capture_.join();
		const auto &finished = *event.finished;
		if (finished)
			send(lease::Ended{lease_id_, lease::Outcome::Committed, finished.value().take_id,
					  to_string(finished.value().stop_reason), finished.value().frames, {}});
		else
			send(lease::Ended{lease_id_, lease::Outcome::Failed, {}, {}, 0, finished.error().code});
		reset_lease();
	}
}

void MediaBroker::capture_loop(std::unique_ptr<CaptureSource> source, std::unique_ptr<TakeWriter> writer,
			       CaptureFormat format)
{
	std::vector<std::uint8_t> samples;
	std::vector<double> peaks(format.channels, -120.0);
	std::optional<MediaError> failure;
	StopReason reason = StopReason::Requested;
	std::uint64_t emitted_frames = 0;
	const std::uint64_t interval_frames =
		std::max<std::uint64_t>(1, static_cast<std::uint64_t>(settings_.level_interval.count()) * format.rate / 1000);
	auto post = [this](CaptureEvent event) {
		std::lock_guard<std::mutex> lock(events_mutex_);
		events_.push_back(std::move(event));
	};
	while (stop_ == kStopNone) {
		if (writer->frames() + format.period_frames > settings_.maximum_frames) {
			reason = StopReason::MaximumDuration;
			break;
		}
		auto period = source->read_period(samples);
		if (!period) {
			if (period.error().code == "MEDIA_CAPTURE.XRUN")
				writer->count_xrun();
			failure = period.error();
			break;
		}
		if (auto appended = writer->append(samples, period.value()); !appended) {
			failure = appended.error();
			break;
		}
		const auto period_peaks = period_peaks_dbfs(format, samples);
		for (std::size_t channel = 0; channel < peaks.size(); ++channel)
			peaks[channel] = std::max(peaks[channel], period_peaks[channel]);
		if (writer->frames() - emitted_frames >= interval_frames) {
			emitted_frames = writer->frames();
			post({peaks, writer->frames() * 1000 / format.rate, std::nullopt});
			std::fill(peaks.begin(), peaks.end(), -120.0);
		}
	}
	source->close();
	if (stop_ == kStopAbort) {
		writer->abandon();
		post({std::nullopt, 0,
		      MediaResult<TakeSummary>::failure(media_error(abort_code_.empty() ? "MEDIA_BROKER.ABORTED" : abort_code_,
								    "Take ended without being kept", "broker.capture"))});
		return;
	}
	if (failure) {
		writer->abandon();
		post({std::nullopt, 0, MediaResult<TakeSummary>::failure(*failure)});
		return;
	}
	post({std::nullopt, 0, writer->finish(reason)});
}
} // namespace media_broker
