#pragma once
#include "bug_reporter/result.h"
#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <regex>
#include <string>
#include <vector>

/**
 * @canonical bug_report_bundle -- `bhp.bug-report/v1` (bug-reports-and-self-repair.md, section 2).
 *
 * One report is a directory: `report.json` (the manifest) and its files,
 * `logs.jsonl.gz` and, for a spoken description, `description.wav`. The
 * whole bundle is private: it goes only to the PC's inbox. Anything public is
 * built later from the manifest's closed fields by the inbox's redactor.
 */
namespace bug_reporter
{
inline constexpr const char *kBundleSchema = "bhp.bug-report/v1";

/** The screen and lifecycle when the report was made; closed tokens from the app descriptors and protocol. */
struct ScreenState {
	std::string surface;
	std::string state;
	std::string display_mode;
	std::string presence;
	std::string solar;
	std::string system;
};

struct ReportDraft {
	/** `voice`, `text` (manual) or `crash`, `restart`, `fault` (automatic). */
	std::string kind;
	/** The service the report is about, `[a-z0-9_.-]{1,64}`. */
	std::string component;
	/** The error code of an automatic report; empty for a manual one. */
	std::string error_code;
	/** The call site or frame that faulted, for the fingerprint. */
	std::string location;
	ScreenState screen;
	std::string description_text;
	/** A spoken description as a mono WAV (voice reports, step 3; description_audio.h). */
	std::optional<std::string> audio_wav;
	/** Structured log lines, oldest first; the newest are kept when the bound cuts. */
	std::vector<std::string> log_lines;
};

struct BundleLimits {
	std::size_t maximum_log_gzip_bytes{512 * 1024};
	std::size_t maximum_text_characters{500};
	std::size_t maximum_trace_ids{64};
	std::uint64_t maximum_audio_bytes{2'000'000};
};

struct BundleFile {
	std::string name;
	std::uint64_t bytes{0};
	std::string sha256;
};

struct Bundle {
	std::string report_id;
	std::string fingerprint;
	nlohmann::json manifest;
	std::vector<BundleFile> files;
	/** The manifest and every file. */
	std::uint64_t bytes{0};
};

inline bool manual_kind(const std::string &kind)
{
	return kind == "voice" || kind == "text";
}
/** `rpt-` and 32 hex digits. */
inline bool valid_report_id(const std::string &report_id)
{
	static const std::regex pattern("rpt-[0-9a-f]{32}");
	return std::regex_match(report_id, pattern);
}
/**
 * Manual reports are never merged, so theirs is unique to the report.
 * Automatic ones hash component, error code and location.
 */
std::string fingerprint(const ReportDraft &draft, const std::string &report_id);
/** The newest distinct `trace_id` values in the lines, at most `maximum`, oldest first. */
std::vector<std::string> trace_ids_of(const std::vector<std::string> &lines, std::size_t maximum);
/** `key=value` lines of the image's version file; `{"image_version": "unavailable"}` without one. */
std::map<std::string, std::string> read_build_info(const std::string &path);
/** gzip (RFC 1952) of the bytes. */
Result<std::string> gzip(const std::string &bytes);
std::string sha256_hex(const std::string &bytes);

/**
 * Writes the bundle into `directory` (made 0700; must not exist yet).
 * REPORT.DRAFT_INVALID for a kind, component, code, text or screen outside its
 * rules; REPORT.STORAGE_UNAVAILABLE when a file cannot be written. Logs are
 * cut from the oldest end until their gzip fits.
 */
Result<Bundle> write_bundle(const std::string &directory, const std::string &report_id, const std::string &created_at,
			    const ReportDraft &draft, const std::map<std::string, std::string> &build,
			    const BundleLimits &limits = {});
} // namespace bug_reporter
