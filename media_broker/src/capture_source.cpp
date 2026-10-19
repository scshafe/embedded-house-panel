#include "media_broker/capture_source.h"
#include <cmath>
#include <thread>
namespace media_broker
{
const char *to_string(const SampleFormat format)
{
	switch (format) {
	case SampleFormat::S16_LE:
		return "S16_LE";
	case SampleFormat::S24_3LE:
		return "S24_3LE";
	case SampleFormat::S32_LE:
		return "S32_LE";
	}
	return "S16_LE";
}

const char *to_string(const TimestampType type)
{
	return type == TimestampType::MonotonicRaw ? "monotonic_raw" : "monotonic";
}

unsigned sample_width(const SampleFormat format)
{
	switch (format) {
	case SampleFormat::S16_LE:
		return 2;
	case SampleFormat::S24_3LE:
		return 3;
	case SampleFormat::S32_LE:
		return 4;
	}
	return 2;
}

FakeCaptureSource::FakeCaptureSource(FakeCaptureFaults faults, const bool paced,
				     const std::uint64_t start_ns, const std::uint32_t seed)
	: faults_(faults), paced_(paced), start_ns_(start_ns), seed_(seed)
{
}

std::int16_t FakeCaptureSource::sample_at(const unsigned channel, const std::uint64_t frame,
					  const std::uint32_t seed)
{
	// A tone of 200 Hz times the channel number at 16 kHz, plus a small
	// deterministic floor, so every channel differs and none is silent.
	constexpr double kPi = 3.14159265358979323846;
	const double tone =
		2000.0 * std::sin(2.0 * kPi * 200.0 * (channel + 1) * static_cast<double>(frame) / 16000.0);
	std::uint32_t x = seed * 2654435761U ^ static_cast<std::uint32_t>(frame * 40503U + channel * 7919U);
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	const int floor_value = static_cast<int>(x % 64U) - 32;
	return static_cast<std::int16_t>(std::lround(tone) + floor_value);
}

MediaResult<CaptureFormat> FakeCaptureSource::open(const CaptureRequest &request)
{
	if (faults_.refuse_open)
		return MediaResult<CaptureFormat>::failure(
			media_error("MEDIA_CAPTURE.OPEN_FAILED", "Fake source refused to open", "capture.open"));
	if (request.format != SampleFormat::S16_LE || request.rate == 0 || request.channels == 0 ||
	    request.period_frames == 0 || request.buffer_frames < 2 * request.period_frames)
		return MediaResult<CaptureFormat>::failure(media_error(
			"MEDIA_CAPTURE.FORMAT_REFUSED", "Fake source grants S16_LE with a buffer of two periods or more",
			"capture.open"));
	format_ = {request.pcm,	       request.format,	      request.rate,
		   request.channels,   request.period_frames, request.buffer_frames,
		   TimestampType::MonotonicRaw, true};
	period_index_ = 0;
	opened_at_ = std::chrono::steady_clock::now();
	open_ = true;
	return MediaResult<CaptureFormat>::success(format_);
}

MediaResult<CapturedPeriod> FakeCaptureSource::read_period(std::vector<std::uint8_t> &samples)
{
	if (!open_)
		return MediaResult<CapturedPeriod>::failure(
			media_error("MEDIA_CAPTURE.NOT_OPEN", "Capture source is not open", "capture.read"));
	const auto index = period_index_++;
	if (faults_.device_lost_at_period && *faults_.device_lost_at_period == index)
		return MediaResult<CapturedPeriod>::failure(
			media_error("MEDIA_CAPTURE.DEVICE_LOST", "Capture device disappeared", "capture.read"));
	if (faults_.xrun_at_period && *faults_.xrun_at_period == index)
		return MediaResult<CapturedPeriod>::failure(
			media_error("MEDIA_CAPTURE.XRUN", "Capture buffer overran", "capture.read"));
	const std::uint64_t first = index * format_.period_frames;
	const std::uint64_t position = first + format_.period_frames;
	if (paced_)
		std::this_thread::sleep_until(opened_at_ + std::chrono::nanoseconds(
								   position * 1'000'000'000ULL / format_.rate));
	samples.resize(format_.period_bytes());
	std::size_t at = 0;
	for (std::uint64_t frame = first; frame < position; ++frame)
		for (unsigned channel = 0; channel < format_.channels; ++channel) {
			const auto value = static_cast<std::uint16_t>(sample_at(channel, frame, seed_));
			samples[at++] = static_cast<std::uint8_t>(value & 0xffU);
			samples[at++] = static_cast<std::uint8_t>(value >> 8U);
		}
	std::uint64_t timestamp = start_ns_ + position * 1'000'000'000ULL / format_.rate;
	if (faults_.timestamp_step && index >= faults_.timestamp_step->first)
		timestamp = static_cast<std::uint64_t>(static_cast<std::int64_t>(timestamp) +
						       faults_.timestamp_step->second);
	return MediaResult<CapturedPeriod>::success({position, timestamp});
}

void FakeCaptureSource::close() noexcept { open_ = false; }
} // namespace media_broker
