#pragma once
#include "media_broker/media_result.h"
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
namespace media_broker
{
enum class SampleFormat { S16_LE, S24_3LE, S32_LE };
enum class TimestampType { MonotonicRaw, Monotonic };

const char *to_string(SampleFormat format);
const char *to_string(TimestampType type);
/** Bytes per sample: 2, 3 or 4. */
unsigned sample_width(SampleFormat format);

/** What the broker asks the device for. The device must grant it exactly. */
struct CaptureRequest {
	std::string pcm;
	SampleFormat format{SampleFormat::S16_LE};
	unsigned rate{16000};
	unsigned channels{6};
	unsigned period_frames{1024};
	unsigned buffer_frames{4096};
};

/** What the device granted; the take record carries every field. */
struct CaptureFormat {
	std::string pcm;
	SampleFormat format{SampleFormat::S16_LE};
	unsigned rate{0};
	unsigned channels{0};
	unsigned period_frames{0};
	unsigned buffer_frames{0};
	TimestampType timestamp_type{TimestampType::MonotonicRaw};
	bool simulated{false};

	unsigned frame_bytes() const { return channels * sample_width(format); }
	std::size_t period_bytes() const { return std::size_t{period_frames} * frame_bytes(); }
};

/**
 * One period as the device delivered it. `position_frames` is the hardware
 * capture position at `timestamp_ns`: the frames read so far, this period
 * included, plus what was still waiting in the buffer.
 */
struct CapturedPeriod {
	std::uint64_t position_frames{0};
	std::uint64_t timestamp_ns{0};
};

/**
 * @extension-point capture_source -- the one place audio enters the broker.
 *
 * `read_period` fills exactly one period of interleaved samples, untouched.
 * An overrun is MEDIA_CAPTURE.XRUN and is never recovered silently; a vanished
 * device is MEDIA_CAPTURE.DEVICE_LOST. After any failure the caller closes.
 */
class CaptureSource
{
public:
	virtual ~CaptureSource() = default;
	virtual MediaResult<CaptureFormat> open(const CaptureRequest &request) = 0;
	virtual MediaResult<CapturedPeriod> read_period(std::vector<std::uint8_t> &samples) = 0;
	virtual void close() noexcept = 0;
};

/** Deterministic fault injection for FakeCaptureSource, by zero-based period index. */
struct FakeCaptureFaults {
	std::optional<std::uint64_t> xrun_at_period;
	std::optional<std::uint64_t> device_lost_at_period;
	/** Adds this many nanoseconds to every timestamp from the period on. */
	std::optional<std::pair<std::uint64_t, std::int64_t>> timestamp_step;
	bool refuse_open{false};
};

/**
 * @canonical fake_capture_source -- the host and simulator audio source.
 *
 * Grants the request as asked (S16_LE only), produces a fixed per-channel tone
 * with a pseudo-random floor drawn from `seed`, and timestamps every period at exactly
 * 1e9 / rate ns per frame from `start_ns`. With `paced`, read_period sleeps
 * until the period's wall time, so a running broker records in real time.
 */
class FakeCaptureSource final : public CaptureSource
{
public:
	explicit FakeCaptureSource(FakeCaptureFaults faults = {}, bool paced = false,
				   std::uint64_t start_ns = 1'000'000'000, std::uint32_t seed = 1);
	MediaResult<CaptureFormat> open(const CaptureRequest &request) override;
	MediaResult<CapturedPeriod> read_period(std::vector<std::uint8_t> &samples) override;
	void close() noexcept override;
	bool is_open() const { return open_; }
	/** The sample value a channel holds at a frame, for tests. */
	static std::int16_t sample_at(unsigned channel, std::uint64_t frame, std::uint32_t seed);

private:
	FakeCaptureFaults faults_;
	bool paced_;
	std::uint64_t start_ns_;
	std::uint32_t seed_;
	bool open_{false};
	CaptureFormat format_{};
	std::uint64_t period_index_{0};
	std::chrono::steady_clock::time_point opened_at_{};
};
} // namespace media_broker
