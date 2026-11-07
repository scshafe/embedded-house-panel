#pragma once
#include "media_broker/media_result.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
namespace media_broker
{
/** A second-order high-pass: its -3 dB corner and its Q. */
struct HighPassDesign {
	double corner_hz;
	double q;
};

/**
 * The low-cut filter for each speaker the mirror unit may carry, matched to the
 * driver in its sealed chamber (docs/hardware/mirror-unit-speaker-and-gate.md,
 * "Playback filter"): `dma45-8` (the chosen driver) and `pui-as04008co` (the
 * fallback). Empty for any other name.
 */
std::optional<HighPassDesign> speaker_high_pass(const std::string &speaker);

/** The speaker the unit carries now. */
inline constexpr const char *kDefaultSpeaker = "dma45-8";

/**
 * @canonical playback_high_pass -- the low-cut filter on everything the host plays.
 *
 * Playback goes to the XVF3800 over USB, and the array takes its echo canceller's
 * reference from that same stream, so filtering on the host keeps the reference equal
 * to what the speaker is driven with. A biquad (RBJ cookbook high-pass, bilinear with
 * the corner prewarped) per channel, run in double and saturated back to 16 bits.
 */
class PlaybackHighPass final
{
public:
	/** MEDIA_PLAYBACK.FILTER_INVALID unless 0 < corner < rate / 2, 0 < q <= 10, 1..32 channels. */
	static MediaResult<PlaybackHighPass> create(unsigned rate, unsigned channels, HighPassDesign design);

	/** Filters interleaved S16 frames in place; the state carries across calls. */
	void process(std::int16_t *samples, std::size_t frames);
	/** Forgets the past input, for a new stream. */
	void reset();
	unsigned channels() const { return channels_; }

private:
	PlaybackHighPass(unsigned channels, std::array<double, 5> coefficients)
		: channels_(channels), coefficients_(coefficients), state_(channels)
	{
	}
	unsigned channels_;
	/** b0, b1, b2, a1, a2, normalised by a0. */
	std::array<double, 5> coefficients_;
	/** Per channel: x[n-1], x[n-2], y[n-1], y[n-2]. */
	std::vector<std::array<double, 4>> state_;
};
} // namespace media_broker
