#pragma once
#include "media_broker/capture_source.h"
#include <cstdint>
#include <memory>
namespace media_broker
{
/**
 * @canonical alsa_capture_source -- the real capture device, through libasound.
 *
 * Opens the named PCM for capture with no resampling, interleaved read access,
 * and the exact format, rate and channel count asked for; any other grant is
 * MEDIA_CAPTURE.FORMAT_REFUSED. The period and buffer sizes are what the device
 * granted near the request, within voice-lab's bounds (256..16384 frames per
 * period, a buffer of two periods or more). Timestamps are enabled with
 * MONOTONIC_RAW (MONOTONIC when the device refuses, as recorded). After each
 * full period, `snd_pcm_htimestamp` gives the frames waiting and the time of
 * the last hardware position update: the pair is (frames read + waiting, time).
 * An overrun is MEDIA_CAPTURE.XRUN; nothing is restarted or padded.
 */
class AlsaCaptureSource final : public CaptureSource
{
public:
	AlsaCaptureSource();
	~AlsaCaptureSource() override;
	MediaResult<CaptureFormat> open(const CaptureRequest &request) override;
	MediaResult<CapturedPeriod> read_period(std::vector<std::uint8_t> &samples) override;
	void close() noexcept override;

private:
	struct State;
	std::unique_ptr<State> state_;
};
} // namespace media_broker
