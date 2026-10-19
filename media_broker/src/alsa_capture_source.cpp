#include "media_broker/alsa_capture_source.h"
#include <alsa/asoundlib.h>
#include <cerrno>
namespace media_broker
{
namespace
{
constexpr unsigned kMinimumPeriodFrames = 256;
constexpr unsigned kMaximumPeriodFrames = 16384;

template <typename Value> MediaResult<Value> fail(const char *code, const char *message, const char *operation,
						 const int alsa_error = 0)
{
	auto error = media_error(code, message, operation);
	if (alsa_error != 0)
		error.context.emplace("alsa_error", snd_strerror(alsa_error));
	return MediaResult<Value>::failure(std::move(error));
}

snd_pcm_format_t alsa_format(const SampleFormat format)
{
	switch (format) {
	case SampleFormat::S16_LE:
		return SND_PCM_FORMAT_S16_LE;
	case SampleFormat::S24_3LE:
		return SND_PCM_FORMAT_S24_3LE;
	case SampleFormat::S32_LE:
		return SND_PCM_FORMAT_S32_LE;
	}
	return SND_PCM_FORMAT_UNKNOWN;
}
} // namespace

struct AlsaCaptureSource::State {
	snd_pcm_t *pcm{nullptr};
	CaptureFormat format;
	std::uint64_t frames_read{0};
};

AlsaCaptureSource::AlsaCaptureSource() : state_(std::make_unique<State>()) {}

AlsaCaptureSource::~AlsaCaptureSource() { close(); }

MediaResult<CaptureFormat> AlsaCaptureSource::open(const CaptureRequest &request)
{
	close();
	if (request.period_frames < kMinimumPeriodFrames || request.period_frames > kMaximumPeriodFrames ||
	    request.buffer_frames < 2 * request.period_frames)
		return fail<CaptureFormat>("MEDIA_CAPTURE.REQUEST_INVALID",
					   "Period must be 256 to 16384 frames with a buffer of two or more",
					   "alsa.open");
	snd_pcm_t *pcm = nullptr;
	if (const int error = snd_pcm_open(&pcm, request.pcm.c_str(), SND_PCM_STREAM_CAPTURE, 0); error < 0)
		return fail<CaptureFormat>(error == -ENOENT || error == -ENODEV ? "MEDIA_CAPTURE.DEVICE_ABSENT"
										: "MEDIA_CAPTURE.OPEN_FAILED",
					   "Capture device did not open", "alsa.open", error);
	auto refuse = [&pcm](const char *code, const char *message, const int error) {
		snd_pcm_close(pcm);
		return fail<CaptureFormat>(code, message, "alsa.open", error);
	};
	snd_pcm_hw_params_t *hardware = nullptr;
	snd_pcm_hw_params_alloca(&hardware);
	unsigned rate = request.rate;
	snd_pcm_uframes_t period = request.period_frames;
	snd_pcm_uframes_t buffer = request.buffer_frames;
	int error = snd_pcm_hw_params_any(pcm, hardware);
	if (error >= 0)
		error = snd_pcm_hw_params_set_rate_resample(pcm, hardware, 0);
	if (error >= 0)
		error = snd_pcm_hw_params_set_access(pcm, hardware, SND_PCM_ACCESS_RW_INTERLEAVED);
	if (error >= 0)
		error = snd_pcm_hw_params_set_format(pcm, hardware, alsa_format(request.format));
	if (error >= 0)
		error = snd_pcm_hw_params_set_channels(pcm, hardware, request.channels);
	if (error >= 0)
		error = snd_pcm_hw_params_set_rate(pcm, hardware, rate, 0);
	if (error < 0)
		return refuse("MEDIA_CAPTURE.FORMAT_REFUSED", "Device does not offer the exact capture mode", error);
	error = snd_pcm_hw_params_set_period_size_near(pcm, hardware, &period, nullptr);
	if (error >= 0)
		error = snd_pcm_hw_params_set_buffer_size_near(pcm, hardware, &buffer);
	if (error >= 0)
		error = snd_pcm_hw_params(pcm, hardware);
	if (error < 0)
		return refuse("MEDIA_CAPTURE.FORMAT_REFUSED", "Device refused the period and buffer sizes", error);
	if (period < kMinimumPeriodFrames || period > kMaximumPeriodFrames || buffer < 2 * period)
		return refuse("MEDIA_CAPTURE.FORMAT_REFUSED", "Granted period or buffer is outside the bounds", 0);

	snd_pcm_sw_params_t *software = nullptr;
	snd_pcm_sw_params_alloca(&software);
	TimestampType timestamp_type = TimestampType::MonotonicRaw;
	error = snd_pcm_sw_params_current(pcm, software);
	if (error >= 0)
		error = snd_pcm_sw_params_set_tstamp_mode(pcm, software, SND_PCM_TSTAMP_ENABLE);
	if (error >= 0 && snd_pcm_sw_params_set_tstamp_type(pcm, software, SND_PCM_TSTAMP_TYPE_MONOTONIC_RAW) < 0) {
		timestamp_type = TimestampType::Monotonic;
		error = snd_pcm_sw_params_set_tstamp_type(pcm, software, SND_PCM_TSTAMP_TYPE_MONOTONIC);
	}
	if (error >= 0)
		error = snd_pcm_sw_params_set_avail_min(pcm, software, period);
	if (error >= 0)
		error = snd_pcm_sw_params(pcm, software);
	if (error >= 0)
		error = snd_pcm_prepare(pcm);
	if (error >= 0)
		error = snd_pcm_start(pcm);
	if (error < 0)
		return refuse("MEDIA_CAPTURE.OPEN_FAILED", "Device refused timestamps or start", error);

	state_->pcm = pcm;
	state_->frames_read = 0;
	state_->format = {request.pcm,
			  request.format,
			  rate,
			  request.channels,
			  static_cast<unsigned>(period),
			  static_cast<unsigned>(buffer),
			  timestamp_type,
			  false};
	return MediaResult<CaptureFormat>::success(state_->format);
}

MediaResult<CapturedPeriod> AlsaCaptureSource::read_period(std::vector<std::uint8_t> &samples)
{
	if (state_->pcm == nullptr)
		return fail<CapturedPeriod>("MEDIA_CAPTURE.NOT_OPEN", "Capture source is not open", "alsa.read");
	const auto &format = state_->format;
	samples.resize(format.period_bytes());
	snd_pcm_uframes_t done = 0;
	while (done < format.period_frames) {
		const snd_pcm_sframes_t got = snd_pcm_readi(
			state_->pcm, samples.data() + done * format.frame_bytes(), format.period_frames - done);
		if (got == -EINTR)
			continue;
		if (got == -EPIPE)
			return fail<CapturedPeriod>("MEDIA_CAPTURE.XRUN", "Capture buffer overran", "alsa.read");
		if (got == -ESTRPIPE)
			return fail<CapturedPeriod>("MEDIA_CAPTURE.SUSPENDED", "Capture device was suspended",
						    "alsa.read");
		if (got == -ENODEV)
			return fail<CapturedPeriod>("MEDIA_CAPTURE.DEVICE_LOST", "Capture device disappeared",
						    "alsa.read");
		if (got < 0)
			return fail<CapturedPeriod>("MEDIA_CAPTURE.READ_FAILED", "Capture read failed", "alsa.read",
						    static_cast<int>(got));
		done += static_cast<snd_pcm_uframes_t>(got);
	}
	state_->frames_read += format.period_frames;
	snd_pcm_uframes_t waiting = 0;
	snd_htimestamp_t timestamp{};
	if (const int error = snd_pcm_htimestamp(state_->pcm, &waiting, &timestamp); error < 0)
		return fail<CapturedPeriod>("MEDIA_CAPTURE.TIMESTAMP_FAILED", "No hardware timestamp", "alsa.read",
					    error);
	return MediaResult<CapturedPeriod>::success(
		{state_->frames_read + waiting, static_cast<std::uint64_t>(timestamp.tv_sec) * 1'000'000'000ULL +
							static_cast<std::uint64_t>(timestamp.tv_nsec)});
}

void AlsaCaptureSource::close() noexcept
{
	if (state_ && state_->pcm != nullptr) {
		snd_pcm_drop(state_->pcm);
		snd_pcm_close(state_->pcm);
		state_->pcm = nullptr;
	}
}
} // namespace media_broker
