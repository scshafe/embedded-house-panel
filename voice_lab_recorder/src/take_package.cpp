#include "voice_lab_recorder/take_package.h"
#include "voice_lab_recorder/canonical_json.h"
#include <array>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <openssl/evp.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace voice_lab_recorder
{
namespace
{
using Json = nlohmann::json;
constexpr const char *kWavName = "original.wav";
constexpr const char *kBrokerRecordName = "capture.json";
constexpr const char *kBrokerRecordSchema = "bhp.media.voice-take/v1";
constexpr std::size_t kMaximumRecordBytes = 4 << 20;
constexpr unsigned kRate = 16000;
// voice-lab audio.py at the pin: the QC thresholds and the silence floor.
constexpr double kSilenceFloorDbfs = -300.0;
constexpr double kQuietPeakDbfs = -35.0;
constexpr double kMaximumAbsDcOffset = 0.02;
constexpr double kMaximumZeroRunMs = 250.0;

Error failure(const char *code, const char *message)
{
	return {code, message, {}, false};
}

template <typename Value> Result<Value> fail(const char *code, const char *message)
{
	return Result<Value>::failure(failure(code, message));
}

class Descriptor final
{
public:
	explicit Descriptor(int descriptor) : descriptor_(descriptor) {}
	~Descriptor()
	{
		if (descriptor_ >= 0)
			::close(descriptor_);
	}
	Descriptor(const Descriptor &) = delete;
	Descriptor &operator=(const Descriptor &) = delete;
	int get() const { return descriptor_; }
	bool close()
	{
		const int descriptor = descriptor_;
		descriptor_ = -1;
		return descriptor < 0 || ::close(descriptor) == 0;
	}

private:
	int descriptor_;
};

bool read_exact(const int descriptor, unsigned char *data, std::size_t size)
{
	while (size > 0) {
		const ssize_t got = ::read(descriptor, data, size);
		if (got < 0 && errno == EINTR)
			continue;
		if (got <= 0)
			return false;
		data += got;
		size -= static_cast<std::size_t>(got);
	}
	return true;
}

bool write_all(const int descriptor, const char *data, std::size_t size)
{
	while (size > 0) {
		const ssize_t written = ::write(descriptor, data, size);
		if (written < 0 && errno == EINTR)
			continue;
		if (written <= 0)
			return false;
		data += written;
		size -= static_cast<std::size_t>(written);
	}
	return true;
}

std::string hex(const unsigned char *digest, unsigned size)
{
	static const char *digits = "0123456789abcdef";
	std::string text;
	for (unsigned at = 0; at < size; ++at) {
		text += digits[digest[at] >> 4];
		text += digits[digest[at] & 15];
	}
	return text;
}

std::string sha256_text(const std::string &bytes)
{
	unsigned char digest[EVP_MAX_MD_SIZE];
	unsigned int size = 0;
	EVP_Digest(bytes.data(), bytes.size(), digest, &size, EVP_sha256(), nullptr);
	return hex(digest, size);
}

/** Copies `source` to a new owner-only file and returns its SHA-256, or empty on failure. */
std::string copy_and_hash(const std::string &source, const std::string &target)
{
	Descriptor input(::open(source.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
	Descriptor output(::open(target.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0600));
	if (input.get() < 0 || output.get() < 0)
		return {};
	EVP_MD_CTX *context = EVP_MD_CTX_new();
	bool ok = context != nullptr && EVP_DigestInit_ex(context, EVP_sha256(), nullptr) == 1;
	std::vector<char> buffer(1 << 20);
	while (ok) {
		const ssize_t got = ::read(input.get(), buffer.data(), buffer.size());
		if (got < 0 && errno == EINTR)
			continue;
		if (got <= 0) {
			ok = got == 0;
			break;
		}
		ok = EVP_DigestUpdate(context, buffer.data(), static_cast<std::size_t>(got)) == 1 &&
		     write_all(output.get(), buffer.data(), static_cast<std::size_t>(got));
	}
	unsigned char digest[EVP_MAX_MD_SIZE];
	unsigned int size = 0;
	ok = ok && EVP_DigestFinal_ex(context, digest, &size) == 1 && size == 32;
	EVP_MD_CTX_free(context);
	ok = ok && fsync(output.get()) == 0 && output.close();
	return ok ? hex(digest, size) : std::string();
}

bool write_record(const std::string &path, const std::string &bytes)
{
	Descriptor output(::open(path.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0600));
	return output.get() >= 0 && write_all(output.get(), bytes.data(), bytes.size()) && fsync(output.get()) == 0 &&
	       output.close();
}

bool fsync_directory(const std::string &path)
{
	Descriptor directory(::open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
	return directory.get() >= 0 && fsync(directory.get()) == 0 && directory.close();
}

void remove_directory(const std::string &path) noexcept
{
	if (DIR *handle = opendir(path.c_str())) {
		std::vector<std::string> names;
		while (const dirent *entry = readdir(handle))
			if (std::strcmp(entry->d_name, ".") != 0 && std::strcmp(entry->d_name, "..") != 0)
				names.emplace_back(entry->d_name);
		closedir(handle);
		for (const auto &name : names)
			::unlink((path + "/" + name).c_str());
	}
	::rmdir(path.c_str());
}

std::optional<Json> read_json(const std::string &path)
{
	Descriptor input(::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
	if (input.get() < 0)
		return std::nullopt;
	std::string text;
	std::array<char, 65536> buffer{};
	for (;;) {
		const ssize_t got = ::read(input.get(), buffer.data(), buffer.size());
		if (got < 0 && errno == EINTR)
			continue;
		if (got < 0 || text.size() + static_cast<std::size_t>(got) > kMaximumRecordBytes)
			return std::nullopt;
		if (got == 0)
			break;
		text.append(buffer.data(), static_cast<std::size_t>(got));
	}
	Json value = Json::parse(text, nullptr, false);
	if (value.is_discarded() || !value.is_object())
		return std::nullopt;
	return value;
}

std::uint32_t le32(const unsigned char *bytes)
{
	return static_cast<std::uint32_t>(bytes[0]) | static_cast<std::uint32_t>(bytes[1]) << 8 |
	       static_cast<std::uint32_t>(bytes[2]) << 16 | static_cast<std::uint32_t>(bytes[3]) << 24;
}

unsigned le16(const unsigned char *bytes)
{
	return static_cast<unsigned>(bytes[0]) | static_cast<unsigned>(bytes[1]) << 8;
}

double dbfs(const double amplitude)
{
	if (amplitude <= 0.0)
		return kSilenceFloorDbfs;
	return std::min(0.0, 20.0 * std::log10(amplitude));
}

/** Python's int / int: exact for the sums a take holds below 2^53, as voice-lab computes them. */
double ratio(const long double numerator, const std::uint64_t denominator)
{
	return static_cast<double>(numerator / static_cast<long double>(denominator));
}

const char *encoding(const unsigned width)
{
	return width == 2 ? "pcm_s16le" : width == 3 ? "pcm_s24le" : "pcm_s32le";
}

unsigned format_width(const std::string &format)
{
	return format == "S16_LE" ? 2 : format == "S24_3LE" ? 3 : format == "S32_LE" ? 4 : 0;
}

std::optional<std::uint64_t> max_deviation_ns(const Json &periods)
{
	std::optional<std::uint64_t> largest;
	for (std::size_t at = 1; at < periods.size(); ++at) {
		const auto position = periods[at][0].get<std::int64_t>() - periods[at - 1][0].get<std::int64_t>();
		const auto elapsed = periods[at][1].get<std::int64_t>() - periods[at - 1][1].get<std::int64_t>();
		const auto deviation = static_cast<std::uint64_t>(std::llabs(elapsed - position * 62'500));
		largest = std::max(largest.value_or(0), deviation);
	}
	return largest;
}

bool periods_valid(const Json &periods)
{
	if (!periods.is_array() || periods.empty())
		return false;
	for (const auto &pair : periods)
		if (!pair.is_array() || pair.size() != 2 || !pair[0].is_number_unsigned() || !pair[1].is_number_unsigned())
			return false;
	return true;
}
} // namespace

bool silent_prompt(const Json &prompt)
{
	const auto category = prompt.value("category", std::string());
	return category == "room-tone" || category == "procedure";
}

Result<Json> xvf3800_capture_profile(const Json &broker_device, const Json &channels, const std::string &room,
				     const unsigned primary_channel)
{
	// voice-lab's Xvf3800Parameter names: the settings a v2 take records.
	static const char *const kSettings[] = {"SHF_BYPASS",    "PP_AGCONOFF",  "PP_AGCGAIN",
						"PP_MIN_NS",     "PP_MIN_NN",    "PP_ECHOONOFF",
						"PP_NLATTENONOFF", "PP_LIMITONOFF", "AEC_HPFONOFF"};
	auto unread = [] { return fail<Json>("VOICE_LAB.DEVICE_UNREAD", "The take's device record is incomplete"); };
	if (!broker_device.is_object() || !broker_device.contains("identity") || !broker_device["identity"].is_object())
		return unread();
	const Json &identity = broker_device["identity"];
	const Json firmware = identity.value("firmware", Json::object());
	const Json read_settings = identity.value("settings", Json::object());
	const Json version = firmware.value("VERSION", Json());
	if (!broker_device.value("model", Json()).is_string() || !broker_device.value("serial", Json()).is_string() ||
	    !broker_device.value("usb_vendor_id", Json()).is_number_unsigned() ||
	    !broker_device.value("usb_product_id", Json()).is_number_unsigned() || !version.is_array() ||
	    version.size() != 3 || !firmware.value("BLD_MSG", Json()).is_string() ||
	    !firmware.value("BLD_REPO_HASH", Json()).is_string())
		return unread();
	Json settings = Json::object();
	for (const char *name : kSettings) {
		const Json value = read_settings.value(name, Json());
		if (!value.is_array() || value.empty())
			return unread();
		settings[name] = value;
	}
	if (!channels.is_array() || primary_channel < 1 || primary_channel > channels.size() || room.empty())
		return fail<Json>("VOICE_LAB.TAKE_PROFILE_MISMATCH", "The channel map, room or primary channel is invalid");
	return Result<Json>::success({{"profile", kCaptureProfileId},
				      {"device",
				       {{"model", broker_device["model"]},
					{"usb_vendor_id", broker_device["usb_vendor_id"]},
					{"usb_product_id", broker_device["usb_product_id"]},
					{"serial", broker_device["serial"]},
					{"firmware",
					 {{"version", version},
					  {"build_message", firmware["BLD_MSG"]},
					  {"build_repo_hash", firmware["BLD_REPO_HASH"]}}}}},
				      {"room", room},
				      {"channels", channels},
				      {"primary_channel", primary_channel},
				      {"settings", settings}});
}

Result<WavInspection> inspect_wav(const std::string &path, const unsigned channel)
{
	using Output = Result<WavInspection>;
	Descriptor input(::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
	std::array<unsigned char, 44> header{};
	if (input.get() < 0 || !read_exact(input.get(), header.data(), header.size()))
		return fail<WavInspection>("VOICE_LAB.TAKE_WAV_INVALID", "The take audio is unreadable");
	WavInspection inspection;
	inspection.channels = le16(&header[22]);
	inspection.sample_rate_hz = le32(&header[24]);
	const unsigned bits = le16(&header[34]);
	inspection.sample_width_bytes = bits / 8;
	const unsigned width = inspection.sample_width_bytes;
	const std::uint32_t data_bytes = le32(&header[40]);
	const unsigned frame_bytes = inspection.channels * width;
	if (std::memcmp(&header[0], "RIFF", 4) != 0 || std::memcmp(&header[8], "WAVEfmt ", 8) != 0 ||
	    le32(&header[16]) != 16 || le16(&header[20]) != 1 || std::memcmp(&header[36], "data", 4) != 0 ||
	    (width != 2 && width != 3 && width != 4) || bits != width * 8 || inspection.channels == 0 ||
	    le16(&header[32]) != frame_bytes || le32(&header[28]) != inspection.sample_rate_hz * frame_bytes ||
	    data_bytes % frame_bytes != 0 || le32(&header[4]) != 36 + data_bytes)
		return fail<WavInspection>("VOICE_LAB.TAKE_WAV_INVALID", "The take audio is not a plain PCM WAV");
	if (channel < 1 || channel > inspection.channels)
		return fail<WavInspection>("VOICE_LAB.TAKE_WAV_INVALID", "The take audio has no such channel");
	inspection.frames = data_bytes / frame_bytes;

	const std::int64_t minimum = -(std::int64_t{1} << (width * 8 - 1));
	const std::int64_t maximum = (std::int64_t{1} << (width * 8 - 1)) - 1;
	std::int64_t total = 0;
	// The sum of squares in two words: one 32-bit sample squared is up to 2^62.
	std::uint64_t squares_low = 0;
	std::uint64_t squares_high = 0;
	std::uint64_t peak = 0;
	std::uint64_t zero_run = 0;
	std::uint64_t longest_zero_run = 0;
	std::uint64_t read_frames = 0;
	std::vector<unsigned char> buffer(static_cast<std::size_t>(frame_bytes) * 4096);
	const std::size_t offset = static_cast<std::size_t>(channel - 1) * width;
	while (read_frames < inspection.frames) {
		const std::uint64_t want = std::min<std::uint64_t>(4096, inspection.frames - read_frames);
		if (!read_exact(input.get(), buffer.data(), static_cast<std::size_t>(want) * frame_bytes))
			return fail<WavInspection>("VOICE_LAB.TAKE_WAV_INVALID", "The take audio is shorter than its header");
		for (std::uint64_t frame = 0; frame < want; ++frame) {
			const unsigned char *sample = &buffer[static_cast<std::size_t>(frame) * frame_bytes + offset];
			std::uint64_t raw = 0;
			for (unsigned at = 0; at < width; ++at)
				raw |= static_cast<std::uint64_t>(sample[at]) << (8 * at);
			std::int64_t value = static_cast<std::int64_t>(raw);
			if (raw & (std::uint64_t{1} << (width * 8 - 1)))
				value -= std::int64_t{1} << (width * 8);
			total += value;
			const auto square = static_cast<std::uint64_t>(value * value);
			squares_low += square;
			if (squares_low < square)
				++squares_high;
			peak = std::max(peak, static_cast<std::uint64_t>(std::llabs(value)));
			if (value == minimum || value == maximum)
				++inspection.clipped_samples;
			if (value == 0) {
				++zero_run;
				longest_zero_run = std::max(longest_zero_run, zero_run);
			} else {
				zero_run = 0;
			}
		}
		read_frames += want;
	}
	unsigned char trailing = 0;
	if (::read(input.get(), &trailing, 1) != 0)
		return fail<WavInspection>("VOICE_LAB.TAKE_WAV_INVALID", "The take audio is longer than its header");

	const double full_scale = static_cast<double>(std::uint64_t{1} << (width * 8 - 1));
	const std::uint64_t count = inspection.frames;
	const double rms = count ? std::sqrt(ratio(static_cast<long double>(squares_high) * 18446744073709551616.0L +
							   static_cast<long double>(squares_low),
						   count)) : 0.0;
	inspection.peak_dbfs = dbfs(static_cast<double>(peak) / full_scale);
	inspection.rms_dbfs = dbfs(rms / full_scale);
	inspection.dc_offset =
		count ? std::max(-1.0, std::min(1.0, ratio(static_cast<long double>(total), count) / full_scale)) : 0.0;
	inspection.longest_zero_run_ms = static_cast<double>(longest_zero_run) /
					 static_cast<double>(inspection.sample_rate_hz) * 1'000.0;
	return Output::success(inspection);
}

Json qc_metrics(const WavInspection &inspection, const bool allow_quiet)
{
	Json findings = Json::array();
	if (inspection.clipped_samples)
		findings.push_back("clipped-samples");
	if (!allow_quiet && inspection.peak_dbfs < kQuietPeakDbfs)
		findings.push_back("peak-too-quiet");
	if (std::fabs(inspection.dc_offset) > kMaximumAbsDcOffset)
		findings.push_back("dc-offset");
	if (inspection.longest_zero_run_ms >= kMaximumZeroRunMs)
		findings.push_back("digital-silence-or-dropout");
	// voice-lab runs its short-zero and buffer-boundary heuristics on its
	// canonical 48 kHz mono format only, so a v2 take never meets them.
	return {{"path", kWavName},
		{"duration_seconds",
		 static_cast<double>(inspection.frames) / static_cast<double>(inspection.sample_rate_hz)},
		{"peak_dbfs", inspection.peak_dbfs},
		{"rms_dbfs", inspection.rms_dbfs},
		{"dc_offset", inspection.dc_offset},
		{"clipped_samples", inspection.clipped_samples},
		{"longest_zero_run_ms", inspection.longest_zero_run_ms},
		{"signal_to_noise_db", nullptr},
		{"passed", findings.empty()},
		{"findings", findings}};
}

Result<TakePackage> write_take_package(const std::string &broker_take_directory, const std::string &package_root,
				       const TakePackageInputs &inputs)
{
	using Output = Result<TakePackage>;
	const auto record = read_json(broker_take_directory + "/" + kBrokerRecordName);
	if (!record)
		return fail<TakePackage>("VOICE_LAB.TAKE_RECORD_INVALID", "The broker's take record is unreadable");
	const Json &capture = *record;
	const std::string take_id = capture.value("take_id", std::string());
	const auto slash = broker_take_directory.find_last_of('/');
	const std::string directory_name =
		slash == std::string::npos ? broker_take_directory : broker_take_directory.substr(slash + 1);
	const Json wav = capture.value("wav", Json::object());
	const unsigned width = format_width(capture.value("format", std::string()));
	if (capture.value("schema_version", std::string()) != kBrokerRecordSchema ||
	    capture.value("purpose", std::string()) != "voice_take" || take_id != directory_name ||
	    take_id.rfind("take-", 0) != 0 || capture.value("xruns", 1) != 0 || capture.value("rate", 0U) != kRate ||
	    width == 0 || wav.value("name", std::string()) != kWavName || !capture.contains("periods") ||
	    !periods_valid(capture["periods"]) || !capture.contains("tolerance_ns") ||
	    !capture["tolerance_ns"].is_number_unsigned() || !capture.contains("frames") ||
	    !capture["frames"].is_number_unsigned() || !capture.contains("simulated") || !capture["simulated"].is_boolean())
		return fail<TakePackage>("VOICE_LAB.TAKE_RECORD_INVALID", "The broker's take record is not a clean take");
	const auto deviation = max_deviation_ns(capture["periods"]);
	const Json recorded_deviation = capture.value("max_abs_deviation_ns", Json());
	if ((deviation ? Json(*deviation) : Json(nullptr)) != recorded_deviation ||
	    (deviation && *deviation > capture["tolerance_ns"].get<std::uint64_t>()))
		return fail<TakePackage>("VOICE_LAB.TAKE_RECORD_INVALID", "The take record's continuity does not hold");

	const Json &profile = inputs.capture_profile;
	const unsigned channels = capture.value("channels", 0U);
	if (!profile.is_object() || profile.value("profile", std::string(kCaptureProfileId)) != kCaptureProfileId ||
	    !profile.contains("channels") || !profile["channels"].is_array() || profile["channels"].size() != channels ||
	    !profile.contains("primary_channel") || !profile["primary_channel"].is_number_unsigned() ||
	    profile["primary_channel"].get<unsigned>() < 1 || profile["primary_channel"].get<unsigned>() > channels)
		return fail<TakePackage>("VOICE_LAB.TAKE_PROFILE_MISMATCH", "The capture profile does not fit the take");

	const std::string package = package_root + "/" + take_id;
	const std::string incoming = package_root + "/.incoming-" + take_id;
	if (::access(package.c_str(), F_OK) == 0)
		return fail<TakePackage>("VOICE_LAB.TAKE_PACKAGE_EXISTS", "That take is already packaged");
	remove_directory(incoming);
	if (::mkdir(incoming.c_str(), 0700) != 0)
		return fail<TakePackage>("VOICE_LAB.TAKE_PACKAGE_WRITE_FAILED", "Cannot start the take package");
	auto abandon = [&incoming](const char *code, const char *message) {
		remove_directory(incoming);
		return fail<TakePackage>(code, message);
	};

	const std::string audio_sha256 = copy_and_hash(broker_take_directory + "/" + kWavName, incoming + "/" + kWavName);
	if (audio_sha256.empty())
		return abandon("VOICE_LAB.TAKE_PACKAGE_WRITE_FAILED", "Cannot copy the take audio");
	const auto inspection = inspect_wav(incoming + "/" + kWavName, profile["primary_channel"].get<unsigned>());
	if (!inspection.has_value())
		return abandon(inspection.error().code.c_str(), "The take audio is not a plain PCM WAV");
	const WavInspection &audio = inspection.value();
	const std::uint64_t wav_bytes = 44 + audio.frames * audio.channels * audio.sample_width_bytes;
	if ("take-" + audio_sha256 != take_id || wav.value("sha256", std::string()) != audio_sha256 ||
	    wav.value("size_bytes", std::uint64_t{0}) != wav_bytes || audio.channels != channels ||
	    audio.sample_rate_hz != kRate || audio.sample_width_bytes != width ||
	    audio.frames != capture["frames"].get<std::uint64_t>())
		return abandon("VOICE_LAB.TAKE_RECORD_INVALID", "The take record disagrees with its audio");

	const Json continuity = {
		{"schema_version", "cole-voice-lab.alsa-continuity/v1"},
		{"simulated", capture["simulated"]},
		{"pcm", capture.value("pcm", std::string())},
		{"access", capture.value("access", std::string())},
		{"format", capture["format"]},
		{"rate", kRate},
		{"channels", channels},
		{"period_frames", capture.value("period_frames", 0U)},
		{"buffer_frames", capture.value("buffer_frames", 0U)},
		{"tstamp_type", capture.value("tstamp_type", std::string())},
		{"xruns", 0},
		{"periods", capture["periods"]},
		{"tolerance_ns", capture["tolerance_ns"]},
		{"max_abs_deviation_ns", recorded_deviation},
		{"frames", audio.frames},
		{"wav", {{"name", kWavName}, {"sha256", audio_sha256}, {"size_bytes", wav_bytes}}},
		{"started_at", capture.value("started_at", std::string())},
		{"stop_reason", capture.value("stop_reason", std::string())},
		{"success", true},
		{"error", nullptr},
	};
	const auto continuity_bytes = canonical_record(continuity);
	const auto prompt_text = canonical_json(inputs.prompt);
	if (!continuity_bytes.has_value() || !prompt_text.has_value())
		return abandon("VOICE_LAB.CANONICAL_JSON_INVALID", "A take record holds a value canonical JSON refuses");

	Json manifest_profile = profile;
	manifest_profile["profile"] = kCaptureProfileId;
	const Json manifest = {
		{"schema_version", "cole-voice-lab.corpus-take/v2"},
		{"take_id", take_id},
		{"subject_id", kCorpusSubject},
		{"prompt", inputs.prompt},
		{"prompt_sha256", sha256_text(prompt_text.value())},
		{"catalog", inputs.catalog},
		{"requested_at", inputs.requested_at},
		{"capture_host", inputs.capture_host},
		{"client", inputs.client},
		{"capture_profile", manifest_profile},
		{"audio",
		 {{"path", kWavName},
		  {"sha256", audio_sha256},
		  {"size_bytes", wav_bytes},
		  {"audio",
		   {{"sample_rate_hz", kRate},
		    {"channels", channels},
		    {"sample_width_bytes", width},
		    {"encoding", encoding(width)},
		    {"frames", audio.frames},
		    {"duration_seconds", static_cast<double>(audio.frames) / static_cast<double>(kRate)}}}}},
		{"continuity",
		 {{"path", "continuity.json"},
		  {"sha256", sha256_text(continuity_bytes.value())},
		  {"size_bytes", continuity_bytes.value().size()},
		  {"pcm", continuity["pcm"]},
		  {"simulated", continuity["simulated"]},
		  {"started_at", continuity["started_at"]},
		  {"stop_reason", continuity["stop_reason"]},
		  {"tolerance_ns", continuity["tolerance_ns"]},
		  {"max_abs_deviation_ns", recorded_deviation}}},
	};
	const auto manifest_bytes = canonical_record(manifest);
	if (!manifest_bytes.has_value())
		return abandon("VOICE_LAB.CANONICAL_JSON_INVALID", "A take record holds a value canonical JSON refuses");
	const std::string manifest_sha256 = sha256_text(manifest_bytes.value());

	const Json metrics = qc_metrics(audio, silent_prompt(inputs.prompt));
	const std::string status = metrics["passed"].get<bool>() ? "pass" : "review";
	const auto qc_bytes = canonical_record({{"schema_version", "cole-voice-lab.qc-report/v1"},
						{"session_id", take_id},
						{"manifest_sha256", manifest_sha256},
						{"generated_at", inputs.qc_generated_at},
						{"status", status},
						{"headphone_review_required", true},
						{"metrics", Json::array({metrics})}});
	if (!qc_bytes.has_value())
		return abandon("VOICE_LAB.CANONICAL_JSON_INVALID", "A take record holds a value canonical JSON refuses");
	const auto ready_bytes = canonical_record({{"schema_version", "cole-voice-lab.ready/v1"},
						   {"session_id", take_id},
						   {"manifest_path", "manifest.json"},
						   {"manifest_sha256", manifest_sha256},
						   {"qc_report_path", "qc.json"},
						   {"qc_report_sha256", sha256_text(qc_bytes.value())},
						   {"finalized_at", inputs.finalized_at}});

	// READY is written last: its presence says the other four are complete.
	if (!write_record(incoming + "/continuity.json", continuity_bytes.value()) ||
	    !write_record(incoming + "/manifest.json", manifest_bytes.value()) ||
	    !write_record(incoming + "/qc.json", qc_bytes.value()) || !ready_bytes.has_value() ||
	    !write_record(incoming + "/READY", ready_bytes.value()) || !fsync_directory(incoming))
		return abandon("VOICE_LAB.TAKE_PACKAGE_WRITE_FAILED", "Cannot write the take package");
	if (::rename(incoming.c_str(), package.c_str()) != 0)
		return abandon("VOICE_LAB.TAKE_PACKAGE_WRITE_FAILED", "Cannot commit the take package");
	if (!fsync_directory(package_root))
		return fail<TakePackage>("VOICE_LAB.TAKE_PACKAGE_COMMIT_UNCERTAIN",
					 "The package was renamed but its directory was not synced");
	return Output::success({take_id, package, manifest_sha256, audio.frames, status});
}
} // namespace voice_lab_recorder
