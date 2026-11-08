#include "bug_reporter/report_bundle.h"
#include <algorithm>
#include <cerrno>
#include <fcntl.h>
#include <fstream>
#include <iterator>
#include <openssl/evp.h>
#include <regex>
#include <set>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

namespace bug_reporter
{
namespace
{
using Json = nlohmann::json;

Error invalid(const std::string &field)
{
	return {"REPORT.DRAFT_INVALID", "The report draft is outside its rules", {{"field", field}}, false};
}

bool token(const std::string &value, std::size_t maximum, bool allow_empty)
{
	static const std::regex pattern("[a-z0-9_.-]*");
	return value.size() <= maximum && (allow_empty || !value.empty()) && std::regex_match(value, pattern);
}

bool error_code(const std::string &value)
{
	static const std::regex pattern("[A-Z][A-Z0-9_]*(\\.[A-Z0-9_]+)+");
	return value.size() <= 96 && std::regex_match(value, pattern);
}

/** UTF-8 characters, or nullopt for invalid UTF-8 or a control character other than a newline. */
std::optional<std::size_t> text_characters(const std::string &text)
{
	std::size_t count = 0;
	for (std::size_t at = 0; at < text.size(); ++count) {
		const unsigned char lead = text[at];
		std::size_t length = lead < 0x80 ? 1 : (lead >> 5) == 6 ? 2 : (lead >> 4) == 14 ? 3 : (lead >> 3) == 30 ? 4 : 0;
		if (length == 0 || at + length > text.size())
			return std::nullopt;
		for (std::size_t i = 1; i < length; ++i)
			if ((static_cast<unsigned char>(text[at + i]) & 0xC0) != 0x80)
				return std::nullopt;
		if (length == 1 && ((lead < 0x20 && lead != '\n') || lead == 0x7f))
			return std::nullopt;
		at += length;
	}
	return count;
}

Result<BundleFile> write_file(const std::string &directory, const std::string &name, const std::string &bytes)
{
	const auto path = directory + "/" + name;
	const int file = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
	bool written = file >= 0 && ::write(file, bytes.data(), bytes.size()) == static_cast<ssize_t>(bytes.size()) &&
		       ::fsync(file) == 0;
	if (file >= 0)
		::close(file);
	if (!written)
		return Result<BundleFile>::failure(
			{"REPORT.STORAGE_UNAVAILABLE", "A bundle file could not be written", {{"file", name}}, true});
	return Result<BundleFile>::success({name, bytes.size(), sha256_hex(bytes)});
}
} // namespace

std::string sha256_hex(const std::string &bytes)
{
	unsigned char digest[EVP_MAX_MD_SIZE];
	unsigned size = 0;
	EVP_Digest(bytes.data(), bytes.size(), digest, &size, EVP_sha256(), nullptr);
	static const char *hex = "0123456789abcdef";
	std::string out;
	for (unsigned i = 0; i < size; ++i) {
		out += hex[digest[i] >> 4];
		out += hex[digest[i] & 15];
	}
	return out;
}

std::string fingerprint(const ReportDraft &draft, const std::string &report_id)
{
	if (manual_kind(draft.kind))
		return sha256_hex("manual\n" + report_id);
	return sha256_hex(draft.component + "\n" + draft.error_code + "\n" + draft.location);
}

std::vector<std::string> trace_ids_of(const std::vector<std::string> &lines, std::size_t maximum)
{
	static const std::regex pattern("[A-Za-z0-9._:-]{1,96}");
	std::vector<std::string> newest;
	std::set<std::string> seen;
	for (auto line = lines.rbegin(); line != lines.rend() && newest.size() < maximum; ++line) {
		const Json record = Json::parse(*line, nullptr, false);
		if (!record.is_object() || !record.contains("trace_id") || !record["trace_id"].is_string())
			continue;
		const auto id = record["trace_id"].get<std::string>();
		if (std::regex_match(id, pattern) && seen.insert(id).second)
			newest.push_back(id);
	}
	return {newest.rbegin(), newest.rend()};
}

std::map<std::string, std::string> read_build_info(const std::string &path)
{
	static const std::regex pattern("([a-z][a-z0-9_]{0,63})=([ -~]{0,200})");
	std::map<std::string, std::string> build;
	std::ifstream input(path);
	for (std::string line; std::getline(input, line) && build.size() < 32;) {
		std::smatch match;
		if (std::regex_match(line, match, pattern))
			build[match[1]] = match[2];
	}
	if (build.empty())
		build["image_version"] = "unavailable";
	return build;
}

Result<std::string> gzip(const std::string &bytes)
{
	z_stream stream{};
	if (deflateInit2(&stream, Z_BEST_COMPRESSION, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK)
		return Result<std::string>::failure({"REPORT.COMPRESSION_FAILED", "gzip could not start", {}, false});
	std::string out(deflateBound(&stream, bytes.size()), '\0');
	stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(bytes.data()));
	stream.avail_in = static_cast<uInt>(bytes.size());
	stream.next_out = reinterpret_cast<Bytef *>(out.data());
	stream.avail_out = static_cast<uInt>(out.size());
	const int status = deflate(&stream, Z_FINISH);
	out.resize(stream.total_out);
	deflateEnd(&stream);
	if (status != Z_STREAM_END)
		return Result<std::string>::failure({"REPORT.COMPRESSION_FAILED", "gzip did not finish", {}, false});
	return Result<std::string>::success(std::move(out));
}

Result<Bundle> write_bundle(const std::string &directory, const std::string &report_id, const std::string &created_at,
			    const ReportDraft &draft, const std::map<std::string, std::string> &build,
			    const BundleLimits &limits)
{
	using Output = Result<Bundle>;
	const bool manual = manual_kind(draft.kind);
	if (!valid_report_id(report_id))
		return Output::failure(invalid("report_id"));
	if (!manual && draft.kind != "crash" && draft.kind != "restart" && draft.kind != "fault")
		return Output::failure(invalid("kind"));
	if (!token(draft.component, 64, false))
		return Output::failure(invalid("component"));
	if (manual ? !draft.error_code.empty() : !error_code(draft.error_code))
		return Output::failure(invalid("error_code"));
	if (draft.location.size() > 256)
		return Output::failure(invalid("location"));
	const auto &screen = draft.screen;
	for (const auto *field : {&screen.surface, &screen.state, &screen.display_mode, &screen.presence, &screen.solar,
				  &screen.system})
		if (!token(*field, 32, true))
			return Output::failure(invalid("screen"));
	const auto characters = text_characters(draft.description_text);
	if (!characters || *characters > limits.maximum_text_characters || (!manual && !draft.description_text.empty()))
		return Output::failure(invalid("description_text"));
	if (draft.audio_path && draft.kind != "voice")
		return Output::failure(invalid("audio_path"));

	if (::mkdir(directory.c_str(), 0700) != 0)
		return Output::failure({"REPORT.STORAGE_UNAVAILABLE", "The report directory could not be made", {}, true});
	Bundle bundle;
	bundle.report_id = report_id;
	bundle.fingerprint = fingerprint(draft, report_id);

	// Keep the newest lines whose gzip fits the bound.
	std::size_t first = 0;
	std::string logs_gz;
	for (;;) {
		std::string joined;
		for (std::size_t at = first; at < draft.log_lines.size(); ++at)
			joined += draft.log_lines[at] + "\n";
		auto compressed = gzip(joined);
		if (!compressed.has_value())
			return Output::failure(compressed.error());
		if (compressed.value().size() <= limits.maximum_log_gzip_bytes || first >= draft.log_lines.size()) {
			logs_gz = std::move(compressed.value());
			break;
		}
		first += std::max<std::size_t>((draft.log_lines.size() - first) / 4, 1);
	}
	auto logs = write_file(directory, "logs.jsonl.gz", logs_gz);
	if (!logs.has_value())
		return Output::failure(logs.error());
	bundle.files.push_back(logs.value());

	if (draft.audio_path) {
		std::ifstream input(*draft.audio_path, std::ios::binary);
		const std::string audio{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
		if (!input.good() && !input.eof())
			return Output::failure({"REPORT.STORAGE_UNAVAILABLE", "The spoken description could not be read", {}, false});
		if (audio.empty() || audio.size() > limits.maximum_audio_bytes)
			return Output::failure(invalid("audio_path"));
		auto wav = write_file(directory, "description.wav", audio);
		if (!wav.has_value())
			return Output::failure(wav.error());
		bundle.files.push_back(wav.value());
	}

	Json files = Json::array();
	for (const auto &file : bundle.files)
		files.push_back({{"name", file.name}, {"bytes", file.bytes}, {"sha256", file.sha256}});
	bundle.manifest = {
		{"schema", kBundleSchema},
		{"report_id", report_id},
		{"kind", draft.kind},
		{"created_at", created_at},
		{"fingerprint", bundle.fingerprint},
		{"component", draft.component},
		{"error_code", manual ? Json(nullptr) : Json(draft.error_code)},
		{"location", draft.location.empty() ? Json(nullptr) : Json(draft.location)},
		{"build", build},
		{"screen",
		 {{"surface", screen.surface}, {"state", screen.state}, {"display_mode", screen.display_mode},
		  {"presence", screen.presence}, {"solar", screen.solar}, {"system", screen.system}}},
		{"trace_ids", trace_ids_of({draft.log_lines.begin() + static_cast<std::ptrdiff_t>(first), draft.log_lines.end()},
					    limits.maximum_trace_ids)},
		{"log_lines", draft.log_lines.size() - first},
		{"description_text", draft.description_text.empty() ? Json(nullptr) : Json(draft.description_text)},
		{"files", files}};
	auto manifest = write_file(directory, "report.json", bundle.manifest.dump() + "\n");
	if (!manifest.has_value())
		return Output::failure(manifest.error());
	bundle.bytes = manifest.value().bytes;
	for (const auto &file : bundle.files)
		bundle.bytes += file.bytes;
	return Output::success(std::move(bundle));
}
} // namespace bug_reporter
