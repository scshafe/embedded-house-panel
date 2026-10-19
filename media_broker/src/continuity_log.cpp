#include "media_broker/continuity_log.h"
#include <cmath>
#include <cstdlib>
namespace media_broker
{
MediaResult<ContinuityLog> ContinuityLog::create(const unsigned rate, const std::size_t maximum_periods)
{
	if (rate == 0 || 1'000'000'000U % rate != 0 || maximum_periods == 0)
		return MediaResult<ContinuityLog>::failure(media_error(
			"MEDIA_CAPTURE.RATE_UNSUPPORTED", "The rate must divide 1e9 exactly", "continuity.create"));
	return MediaResult<ContinuityLog>::success(ContinuityLog(1'000'000'000U / rate, maximum_periods));
}

MediaResult<Done> ContinuityLog::add(const CapturedPeriod &period)
{
	if (periods_.size() >= maximum_periods_)
		return MediaResult<Done>::failure(
			media_error("MEDIA_TAKE.TOO_LONG", "Take exceeds its period bound", "continuity.add"));
	if (!periods_.empty()) {
		const auto &[last_position, last_timestamp] = periods_.back();
		if (period.position_frames <= last_position || period.timestamp_ns <= last_timestamp)
			return MediaResult<Done>::failure(media_error(
				"MEDIA_CAPTURE.CONTINUITY_ORDER",
				"Hardware position and timestamp must both increase", "continuity.add"));
		const auto elapsed = static_cast<std::int64_t>(period.timestamp_ns - last_timestamp);
		const auto expected =
			static_cast<std::int64_t>((period.position_frames - last_position) * ns_per_frame_);
		const auto deviation = static_cast<std::uint64_t>(std::llabs(elapsed - expected));
		if (!max_abs_deviation_ns_ || deviation > *max_abs_deviation_ns_)
			max_abs_deviation_ns_ = deviation;
	}
	periods_.push_back({period.position_frames, period.timestamp_ns});
	return MediaResult<Done>::success({});
}

std::vector<double> period_peaks_dbfs(const CaptureFormat &format, const std::vector<std::uint8_t> &samples)
{
	const unsigned width = sample_width(format.format);
	std::vector<double> peaks(format.channels, -120.0);
	if (format.channels == 0 || samples.size() % format.frame_bytes() != 0)
		return peaks;
	const double full_scale = std::ldexp(1.0, static_cast<int>(width * 8 - 1));
	std::vector<std::int64_t> largest(format.channels, 0);
	for (std::size_t at = 0; at < samples.size(); at += width) {
		std::int64_t value = 0;
		for (unsigned byte = 0; byte < width; ++byte)
			value |= static_cast<std::int64_t>(samples[at + byte]) << (8 * byte);
		const std::int64_t sign = std::int64_t{1} << (width * 8 - 1);
		value = (value ^ sign) - sign;
		const auto channel = (at / width) % format.channels;
		if (std::llabs(value) > largest[channel])
			largest[channel] = std::llabs(value);
	}
	for (unsigned channel = 0; channel < format.channels; ++channel)
		if (largest[channel] > 0)
			peaks[channel] = std::max(-120.0, 20.0 * std::log10(largest[channel] / full_scale));
	return peaks;
}
} // namespace media_broker
