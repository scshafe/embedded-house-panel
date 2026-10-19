#pragma once
#include "media_broker/capture_source.h"
#include <array>
#include <cstdint>
#include <optional>
#include <vector>
namespace media_broker
{
/**
 * @canonical continuity_log -- per-period hardware positions and timestamps.
 *
 * Follows the rule voice-lab built for `alsa-continuity/v1` (voice-lab main
 * d89d530, `period_deviations_ns`): deviation[i] = (ts[i] - ts[i-1]) -
 * (pos[i] - pos[i-1]) * (1e9 / rate), with the rate dividing 1e9 exactly.
 * Positions and timestamps must strictly increase. The pinned contract, when
 * voice-lab publishes it, is authoritative over this comment.
 */
class ContinuityLog final
{
public:
	/** Fails for a rate that does not divide 1e9, or a period count of zero. */
	static MediaResult<ContinuityLog> create(unsigned rate, std::size_t maximum_periods);

	/** MEDIA_CAPTURE.CONTINUITY_ORDER for a pair that does not increase both values. */
	MediaResult<Done> add(const CapturedPeriod &period);
	void count_xrun() { ++xruns_; }

	const std::vector<std::array<std::uint64_t, 2>> &periods() const { return periods_; }
	std::uint64_t xruns() const { return xruns_; }
	/** Empty with fewer than two pairs. */
	std::optional<std::uint64_t> max_abs_deviation_ns() const { return max_abs_deviation_ns_; }
	std::uint64_t ns_per_frame() const { return ns_per_frame_; }

private:
	ContinuityLog(std::uint64_t ns_per_frame, std::size_t maximum_periods)
		: ns_per_frame_(ns_per_frame), maximum_periods_(maximum_periods)
	{
	}
	std::uint64_t ns_per_frame_;
	std::size_t maximum_periods_;
	std::vector<std::array<std::uint64_t, 2>> periods_;
	std::uint64_t xruns_{0};
	std::optional<std::uint64_t> max_abs_deviation_ns_;
};

/** Peak absolute sample per channel over one period, in dBFS (-120 for silence). */
std::vector<double> period_peaks_dbfs(const CaptureFormat &format,
				      const std::vector<std::uint8_t> &samples);
} // namespace media_broker
