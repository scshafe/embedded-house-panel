#include "media_broker/playback_filter.h"
#include <algorithm>
#include <cmath>
namespace media_broker
{
std::optional<HighPassDesign> speaker_high_pass(const std::string &speaker)
{
	// Butterworth sections. The DMA45-8's sealed box is itself a second-order high-pass
	// (Fc about 221 Hz, Qtc about 0.86); 150 Hz, its free-air resonance, adds a second
	// order below it, so content it cannot play stops driving the cone and the
	// amplifier. The PUI's 40 x 20 mm class reaches its 0.1 mm travel near 200 Hz.
	if (speaker == "dma45-8")
		return HighPassDesign{150.0, M_SQRT1_2};
	if (speaker == "pui-as04008co")
		return HighPassDesign{500.0, M_SQRT1_2};
	return std::nullopt;
}

MediaResult<PlaybackHighPass> PlaybackHighPass::create(const unsigned rate, const unsigned channels,
							const HighPassDesign design)
{
	if (rate == 0 || channels == 0 || channels > 32 || !(design.corner_hz > 0.0) ||
	    !(design.corner_hz < rate / 2.0) || !(design.q > 0.0) || !(design.q <= 10.0))
		return MediaResult<PlaybackHighPass>::failure(media_error(
			"MEDIA_PLAYBACK.FILTER_INVALID",
			"The corner must lie below half the rate, with a positive Q and 1-32 channels",
			"playback_filter.create"));
	const double w0 = 2.0 * M_PI * design.corner_hz / rate;
	const double cosine = std::cos(w0);
	const double alpha = std::sin(w0) / (2.0 * design.q);
	const double a0 = 1.0 + alpha;
	return MediaResult<PlaybackHighPass>::success(PlaybackHighPass(
		channels, {(1.0 + cosine) / 2.0 / a0, -(1.0 + cosine) / a0, (1.0 + cosine) / 2.0 / a0,
			   -2.0 * cosine / a0, (1.0 - alpha) / a0}));
}

void PlaybackHighPass::process(std::int16_t *samples, const std::size_t frames)
{
	const auto &[b0, b1, b2, a1, a2] = coefficients_;
	for (std::size_t frame = 0; frame < frames; ++frame) {
		for (unsigned channel = 0; channel < channels_; ++channel) {
			auto &[x1, x2, y1, y2] = state_[channel];
			std::int16_t &sample = samples[frame * channels_ + channel];
			const double x = sample;
			const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
			x2 = x1;
			x1 = x;
			y2 = y1;
			y1 = y;
			sample = static_cast<std::int16_t>(std::clamp(std::lround(y), -32768L, 32767L));
		}
	}
}

void PlaybackHighPass::reset() { std::fill(state_.begin(), state_.end(), std::array<double, 4>{}); }
} // namespace media_broker
