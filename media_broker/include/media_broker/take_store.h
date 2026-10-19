#pragma once
#include "media_broker/capture_source.h"
#include "media_broker/continuity_log.h"
#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>
namespace media_broker
{
/** The panel's own take record (plan voice-take-capture, Decision 1). */
constexpr const char *kTakeRecordSchema = "bhp.media.voice-take/v1";

enum class StopReason { Requested, MaximumDuration };
const char *to_string(StopReason reason);

struct TakeLimits {
	std::size_t maximum_takes{0};
	std::uint64_t maximum_bytes{0};
	/** Free space that must remain on the filesystem after a full-length take. */
	std::uint64_t free_space_margin_bytes{0};
};

/**
 * @canonical take_store -- the broker's bounded, owner-only directory of takes.
 *
 * Each completed take is a directory named by its take ID ("take-" + the
 * SHA-256 of original.wav) holding exactly original.wav and capture.json.
 * Partial takes live in ".incoming-*" directories and are removed at open.
 */
class TakeStore final
{
public:
	/**
	 * Creates the root (0700) if missing. Fails with MEDIA_TAKE.STORE_UNSAFE when
	 * the root is not a directory owned by this user with no group or other access.
	 */
	static MediaResult<TakeStore> open(std::string root, TakeLimits limits);

	/**
	 * MEDIA_TAKE.STORE_FULL when another take of `planned_bytes` would pass the
	 * count or byte ceiling, MEDIA_TAKE.NO_SPACE when the filesystem lacks room.
	 */
	MediaResult<Done> admit(std::uint64_t planned_bytes) const;
	std::vector<std::string> take_ids() const;
	const std::string &root() const { return root_; }

private:
	TakeStore(std::string root, TakeLimits limits) : root_(std::move(root)), limits_(limits) {}
	std::string root_;
	TakeLimits limits_;
};

struct TakeSettings {
	/** Wall-clock start as RFC 3339 UTC ("2026-10-19T12:00:00Z"). */
	std::string started_at;
	std::uint64_t tolerance_ns{0};
	std::uint64_t maximum_frames{0};
	/** Device, firmware, routing and settings read at the start; null when unread. */
	nlohmann::json device;
};

struct TakeSummary {
	std::string take_id;
	StopReason stop_reason{StopReason::Requested};
	std::uint64_t frames{0};
	std::uint64_t wav_bytes{0};
	std::optional<std::uint64_t> max_abs_deviation_ns;
};

/**
 * @canonical take_writer -- streams one take to disk and commits it whole.
 *
 * Samples are written exactly as delivered behind a plain 44-byte PCM header.
 * `finish` commits only a clean take: zero xruns, at least one period, and the
 * maximum deviation within the tolerance; anything else is erased and fails.
 * Commit is fsync of both files and the directory, a rename, and an fsync of
 * the root. Destroying an unfinished writer erases its partial take.
 */
class TakeWriter final
{
public:
	static MediaResult<std::unique_ptr<TakeWriter>>
	begin(const TakeStore &store, const CaptureFormat &format, TakeSettings settings);
	~TakeWriter();
	TakeWriter(const TakeWriter &) = delete;
	TakeWriter &operator=(const TakeWriter &) = delete;

	/** One period; MEDIA_TAKE.TOO_LONG past maximum_frames, MEDIA_TAKE.WRITE_FAILED on I/O. */
	MediaResult<Done> append(const std::vector<std::uint8_t> &samples,
				 const CapturedPeriod &period);
	void count_xrun() { log_.count_xrun(); }
	MediaResult<TakeSummary> finish(StopReason reason);
	/** Erases the partial take; safe to call more than once. */
	void abandon() noexcept;

	std::uint64_t frames() const { return frames_; }
	const ContinuityLog &continuity() const { return log_; }

private:
	TakeWriter(std::string root, std::string directory, int wav, CaptureFormat format,
		   TakeSettings settings, ContinuityLog log);
	std::string root_;
	std::string directory_;
	int wav_;
	CaptureFormat format_;
	TakeSettings settings_;
	ContinuityLog log_;
	std::uint64_t frames_{0};
	bool done_{false};
};

/** The 44-byte PCM WAV header for `frames` frames of `format`. */
std::vector<std::uint8_t> wav_header(const CaptureFormat &format, std::uint64_t frames);
} // namespace media_broker
