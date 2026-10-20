#pragma once
#include "voice_lab_recorder/result.h"
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>

/**
 * @canonical voice_lab_take_package -- a broker take as voice-lab's corpus-take/v2.
 *
 * corpus-client.v1 PROTOCOL.md section 3. From one committed media-broker take
 * (`original.wav` and `capture.json`, take store format bhp.media.voice-take/v1)
 * and what the recorder knows about the prompt, the catalog and the capture
 * profile, the packager writes the five files: `original.wav` unchanged,
 * `continuity.json` (alsa-continuity/v1), `manifest.json`, `qc.json`, and
 * `READY` last. Every record is canonical JSON with one final newline. The
 * package is assembled in a `.incoming-` directory and renamed into place, so a
 * package directory is complete or absent. Times are inputs, never read here.
 */
namespace voice_lab_recorder
{
inline constexpr const char *kCaptureProfileId = "xvf3800-linear-4";
inline constexpr const char *kCorpusSubject = "cole-self";

/** One channel of a plain-header PCM WAV, measured as voice-lab's QC measures it. */
struct WavInspection {
	unsigned channels{0};
	unsigned sample_rate_hz{0};
	unsigned sample_width_bytes{0};
	std::uint64_t frames{0};
	double peak_dbfs{0};
	double rms_dbfs{0};
	double dc_offset{0};
	std::uint64_t clipped_samples{0};
	double longest_zero_run_ms{0};
};

/**
 * Measures channel `channel` (1-based) of the WAV at `path`: peak, RMS and DC
 * from exact integer sums, clipped samples at either rail, and the longest run
 * of exact zeros. VOICE_LAB.TAKE_WAV_INVALID for a header that is not the plain
 * 44-byte PCM header, a data length that disagrees with it, or no such channel.
 */
Result<WavInspection> inspect_wav(const std::string &path, unsigned channel);

/** voice-lab's AudioQcMetrics for `original.wav`; a quiet peak is allowed for a silent prompt. */
nlohmann::json qc_metrics(const WavInspection &inspection, bool allow_quiet);

/** A prompt is silent when its category is `room-tone` or `procedure` (voice-lab D-013). */
bool silent_prompt(const nlohmann::json &prompt);

/**
 * The CaptureProfile for a take, from the broker's device record (`capture.json`
 * `device`: the USB description and the array's identity read at the start)
 * and the board's channel map, room and primary channel, which are
 * configuration: the array reports none of them. VOICE_LAB.DEVICE_UNREAD when
 * the record lacks the serial, the firmware or a setting voice-lab requires,
 * so an unread value is never filled in.
 */
Result<nlohmann::json> xvf3800_capture_profile(const nlohmann::json &broker_device, const nlohmann::json &channels,
					       const std::string &room, unsigned primary_channel);

struct TakePackageInputs {
	/** The catalog version's prompt, exactly as voice-lab serves it. */
	nlohmann::json prompt;
	/** The catalog version (CatalogReference). */
	nlohmann::json catalog;
	/** The capture profile (CaptureProfile): device, room, channel map, primary channel, settings. */
	nlohmann::json capture_profile;
	/** TakeClient: name, version and platform. */
	nlohmann::json client;
	std::string capture_host;
	/** RFC 3339 UTC: when the prompt was requested, QC ran, and the package was finished. */
	std::string requested_at;
	std::string qc_generated_at;
	std::string finalized_at;
};

struct TakePackage {
	std::string take_id;
	std::string directory;
	std::string manifest_sha256;
	std::uint64_t frames{0};
	/** "pass" or "review", as qc.json says. */
	std::string qc_status;
};

/**
 * Packages the broker take in `broker_take_directory` into
 * `<package_root>/<take id>/`. VOICE_LAB.TAKE_RECORD_INVALID when capture.json
 * is not a clean committed take or disagrees with its WAV,
 * VOICE_LAB.TAKE_PROFILE_MISMATCH when the capture profile does not fit the
 * WAV, VOICE_LAB.TAKE_PACKAGE_EXISTS when that package is already there, and
 * VOICE_LAB.TAKE_PACKAGE_WRITE_FAILED on I/O (the partial package is erased).
 */
Result<TakePackage> write_take_package(const std::string &broker_take_directory, const std::string &package_root,
				       const TakePackageInputs &inputs);
} // namespace voice_lab_recorder
