#pragma once
#include "bug_reporter/result.h"
#include <cstdint>
#include <string>

/**
 * A spoken bug description, from the broker's take to the bundle
 * (bug-reports-and-self-repair.md, step 3).
 */
namespace bug_reporter
{
/**
 * One channel of a broker take (`original.wav`: 16-bit PCM, interleaved, the
 * array's 6 channels) as a mono 16-bit WAV of at most `maximum_frames`. The
 * array records 6 channels; a bundle carries one, so 60 s fit its 2 MB.
 * REPORT.AUDIO_INVALID for anything but a 16-bit PCM WAV with that channel.
 */
Result<std::string> mono_description(const std::string &wav_path, unsigned channel, std::uint64_t maximum_frames);
/** `take-` and 64 hex digits, as the broker names takes. */
bool valid_take_id(const std::string &take_id);
} // namespace bug_reporter
