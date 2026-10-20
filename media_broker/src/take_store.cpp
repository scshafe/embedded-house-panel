#include "media_broker/take_store.h"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <openssl/evp.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
namespace media_broker
{
namespace
{
constexpr const char *kIncomingPrefix = ".incoming-";
constexpr const char *kWavName = "original.wav";
constexpr const char *kRecordName = "capture.json";

template <typename Value> MediaResult<Value> fail(const char *code, const char *message, const char *operation)
{
	return MediaResult<Value>::failure(media_error(code, message, operation));
}

bool starts_with(const std::string &text, const char *prefix)
{
	return text.rfind(prefix, 0) == 0;
}

bool is_take_id(const std::string &name)
{
	if (name.size() != 5 + 64 || !starts_with(name, "take-"))
		return false;
	for (std::size_t at = 5; at < name.size(); ++at)
		if (!((name[at] >= '0' && name[at] <= '9') || (name[at] >= 'a' && name[at] <= 'f')))
			return false;
	return true;
}

std::vector<std::string> entries(const std::string &directory)
{
	std::vector<std::string> names;
	if (DIR *handle = opendir(directory.c_str())) {
		while (const dirent *entry = readdir(handle)) {
			const std::string name = entry->d_name;
			if (name != "." && name != "..")
				names.push_back(name);
		}
		closedir(handle);
	}
	return names;
}

/** Removes a take directory: its files, then the directory. Takes hold no subdirectories. */
void remove_take_directory(const std::string &directory) noexcept
{
	for (const auto &name : entries(directory))
		unlink((directory + "/" + name).c_str());
	rmdir(directory.c_str());
}

bool fsync_path(const std::string &path, const int flags)
{
	const int descriptor = ::open(path.c_str(), flags | O_CLOEXEC);
	if (descriptor < 0)
		return false;
	const bool synced = fsync(descriptor) == 0;
	return ::close(descriptor) == 0 && synced;
}

bool write_all(const int descriptor, const std::uint8_t *data, std::size_t size)
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

bool pwrite_all(const int descriptor, const std::vector<std::uint8_t> &data, const off_t offset)
{
	std::size_t done = 0;
	while (done < data.size()) {
		const ssize_t written =
			::pwrite(descriptor, data.data() + done, data.size() - done, offset + static_cast<off_t>(done));
		if (written < 0 && errno == EINTR)
			continue;
		if (written <= 0)
			return false;
		done += static_cast<std::size_t>(written);
	}
	return true;
}

/** Lowercase hex SHA-256 of a whole file, or empty on failure. */
std::string sha256_file(const std::string &path)
{
	const int descriptor = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
	if (descriptor < 0)
		return {};
	EVP_MD_CTX *context = EVP_MD_CTX_new();
	bool ok = context != nullptr && EVP_DigestInit_ex(context, EVP_sha256(), nullptr) == 1;
	std::vector<unsigned char> buffer(1 << 16);
	while (ok) {
		const ssize_t got = ::read(descriptor, buffer.data(), buffer.size());
		if (got < 0 && errno == EINTR)
			continue;
		if (got < 0)
			ok = false;
		if (got <= 0)
			break;
		ok = EVP_DigestUpdate(context, buffer.data(), static_cast<std::size_t>(got)) == 1;
	}
	unsigned char digest[EVP_MAX_MD_SIZE];
	unsigned int size = 0;
	ok = ok && EVP_DigestFinal_ex(context, digest, &size) == 1 && size == 32;
	EVP_MD_CTX_free(context);
	::close(descriptor);
	if (!ok)
		return {};
	static const char *hex = "0123456789abcdef";
	std::string text;
	for (unsigned at = 0; at < size; ++at) {
		text += hex[digest[at] >> 4];
		text += hex[digest[at] & 15];
	}
	return text;
}

std::uint64_t directory_bytes(const std::string &directory)
{
	std::uint64_t total = 0;
	struct stat status {};
	for (const auto &name : entries(directory))
		if (lstat((directory + "/" + name).c_str(), &status) == 0 && S_ISREG(status.st_mode))
			total += static_cast<std::uint64_t>(status.st_size);
	return total;
}

void put_le(std::vector<std::uint8_t> &out, std::uint32_t value, const unsigned bytes)
{
	for (unsigned at = 0; at < bytes; ++at, value >>= 8)
		out.push_back(static_cast<std::uint8_t>(value & 0xffU));
}
} // namespace

const char *to_string(const StopReason reason)
{
	return reason == StopReason::Requested ? "requested" : "maximum-duration";
}

std::vector<std::uint8_t> wav_header(const CaptureFormat &format, const std::uint64_t frames)
{
	const std::uint32_t width = sample_width(format.format);
	const auto data_bytes = static_cast<std::uint32_t>(frames * format.frame_bytes());
	std::vector<std::uint8_t> header;
	header.reserve(44);
	for (const char c : std::string("RIFF"))
		header.push_back(static_cast<std::uint8_t>(c));
	put_le(header, 36 + data_bytes, 4);
	for (const char c : std::string("WAVEfmt "))
		header.push_back(static_cast<std::uint8_t>(c));
	put_le(header, 16, 4);
	put_le(header, 1, 2);
	put_le(header, format.channels, 2);
	put_le(header, format.rate, 4);
	put_le(header, format.rate * format.frame_bytes(), 4);
	put_le(header, format.frame_bytes(), 2);
	put_le(header, width * 8, 2);
	for (const char c : std::string("data"))
		header.push_back(static_cast<std::uint8_t>(c));
	put_le(header, data_bytes, 4);
	return header;
}

MediaResult<TakeStore> TakeStore::open(std::string root, const TakeLimits limits)
{
	if (root.empty() || root.front() != '/' || limits.maximum_takes == 0 || limits.maximum_bytes == 0)
		return fail<TakeStore>("MEDIA_TAKE.STORE_INVALID",
				       "Take store needs an absolute root and nonzero limits", "take_store.open");
	if (mkdir(root.c_str(), 0700) != 0 && errno != EEXIST)
		return fail<TakeStore>("MEDIA_TAKE.STORE_UNAVAILABLE", "Take store root cannot be created",
				       "take_store.open");
	struct stat status {};
	if (lstat(root.c_str(), &status) != 0 || !S_ISDIR(status.st_mode) || status.st_uid != geteuid() ||
	    (status.st_mode & 077) != 0)
		return fail<TakeStore>("MEDIA_TAKE.STORE_UNSAFE",
				       "Take store root must be an owner-only directory of this user",
				       "take_store.open");
	for (const auto &name : entries(root))
		if (starts_with(name, kIncomingPrefix))
			remove_take_directory(root + "/" + name);
	return MediaResult<TakeStore>::success(TakeStore(std::move(root), limits));
}

std::vector<std::string> TakeStore::take_ids() const
{
	std::vector<std::string> ids;
	for (const auto &name : entries(root_))
		if (is_take_id(name))
			ids.push_back(name);
	return ids;
}

MediaResult<Done> TakeStore::discard(const std::string &take_id) const
{
	const std::string directory = root_ + "/" + take_id;
	struct stat status {};
	if (!is_take_id(take_id) || lstat(directory.c_str(), &status) != 0 || !S_ISDIR(status.st_mode))
		return fail<Done>("MEDIA_TAKE.UNKNOWN", "No committed take has that ID", "take_store.discard");
	remove_take_directory(directory);
	if (access(directory.c_str(), F_OK) == 0)
		return fail<Done>("MEDIA_TAKE.DISCARD_FAILED", "The take could not be removed", "take_store.discard");
	fsync_path(root_, O_RDONLY | O_DIRECTORY);
	return MediaResult<Done>::success({});
}

MediaResult<Done> TakeStore::hand_off(const std::string &take_id, const std::string &handoff_directory) const
{
	const std::string directory = root_ + "/" + take_id;
	struct stat status {};
	if (!is_take_id(take_id) || lstat(directory.c_str(), &status) != 0 || !S_ISDIR(status.st_mode))
		return fail<Done>("MEDIA_TAKE.UNKNOWN", "No committed take has that ID", "take_store.hand_off");
	const std::string target = handoff_directory + "/" + take_id;
	if (lstat(target.c_str(), &status) != 0) {
		const std::string incoming = handoff_directory + "/" + kIncomingPrefix + take_id;
		remove_take_directory(incoming);
		// The hand-off directory's group is shared by the broker and the uploader
		// alone (2770): the uploader reads the take and removes it when packaged.
		bool copied = mkdir(incoming.c_str(), 0770) == 0 && chmod(incoming.c_str(), 0770) == 0;
		for (const char *name : {kWavName, kRecordName}) {
			if (!copied)
				break;
			const int input = ::open((directory + "/" + name).c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
			const int output =
				::open((incoming + "/" + name).c_str(), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0640);
			std::vector<std::uint8_t> buffer(1 << 20);
			copied = input >= 0 && output >= 0 && fchmod(output, 0640) == 0;
			while (copied) {
				const ssize_t got = ::read(input, buffer.data(), buffer.size());
				if (got < 0 && errno == EINTR)
					continue;
				if (got <= 0) {
					copied = got == 0;
					break;
				}
				copied = write_all(output, buffer.data(), static_cast<std::size_t>(got));
			}
			copied = copied && fsync(output) == 0;
			if (input >= 0)
				::close(input);
			if (output >= 0)
				copied = ::close(output) == 0 && copied;
		}
		copied = copied && fsync_path(incoming, O_RDONLY | O_DIRECTORY) &&
			 rename(incoming.c_str(), target.c_str()) == 0 &&
			 fsync_path(handoff_directory, O_RDONLY | O_DIRECTORY);
		if (!copied) {
			remove_take_directory(incoming);
			return fail<Done>("MEDIA_TAKE.HANDOFF_FAILED", "The take could not be handed over",
					  "take_store.hand_off");
		}
	}
	return discard(take_id);
}

MediaResult<Done> TakeStore::admit(const std::uint64_t planned_bytes) const
{
	const auto ids = take_ids();
	std::uint64_t used = 0;
	for (const auto &id : ids)
		used += directory_bytes(root_ + "/" + id);
	if (ids.size() >= limits_.maximum_takes || used + planned_bytes > limits_.maximum_bytes)
		return fail<Done>("MEDIA_TAKE.STORE_FULL", "Take store has no room for another take",
				  "take_store.admit");
	struct statvfs filesystem {};
	if (statvfs(root_.c_str(), &filesystem) != 0)
		return fail<Done>("MEDIA_TAKE.STORE_UNAVAILABLE", "Take store filesystem is unreadable",
				  "take_store.admit");
	const auto available = static_cast<std::uint64_t>(filesystem.f_bavail) * filesystem.f_frsize;
	if (available < planned_bytes + limits_.free_space_margin_bytes)
		return fail<Done>("MEDIA_TAKE.NO_SPACE", "Filesystem lacks room for a full-length take",
				  "take_store.admit");
	return MediaResult<Done>::success({});
}

TakeWriter::TakeWriter(std::string root, std::string directory, const int wav, CaptureFormat format,
		       TakeSettings settings, ContinuityLog log)
	: root_(std::move(root)), directory_(std::move(directory)), wav_(wav), format_(std::move(format)),
	  settings_(std::move(settings)), log_(std::move(log))
{
}

TakeWriter::~TakeWriter() { abandon(); }

MediaResult<std::unique_ptr<TakeWriter>> TakeWriter::begin(const TakeStore &store, const CaptureFormat &format,
							   TakeSettings settings)
{
	using Result = MediaResult<std::unique_ptr<TakeWriter>>;
	if (format.period_frames == 0 || settings.maximum_frames < format.period_frames ||
	    settings.tolerance_ns == 0)
		return fail<std::unique_ptr<TakeWriter>>("MEDIA_TAKE.SETTINGS_INVALID",
							 "Take needs a period, a maximum length and a tolerance",
							 "take_writer.begin");
	auto log = ContinuityLog::create(format.rate, settings.maximum_frames / format.period_frames + 1);
	if (!log)
		return Result::failure(log.error());
	std::string pattern = store.root() + "/" + kIncomingPrefix + "XXXXXX";
	if (mkdtemp(pattern.data()) == nullptr)
		return fail<std::unique_ptr<TakeWriter>>("MEDIA_TAKE.WRITE_FAILED", "Cannot create a partial take",
							 "take_writer.begin");
	const int wav = ::open((pattern + "/" + kWavName).c_str(), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0600);
	const auto header = wav_header(format, 0);
	if (wav < 0 || !write_all(wav, header.data(), header.size())) {
		if (wav >= 0)
			::close(wav);
		remove_take_directory(pattern);
		return fail<std::unique_ptr<TakeWriter>>("MEDIA_TAKE.WRITE_FAILED", "Cannot start the take audio",
							 "take_writer.begin");
	}
	return Result::success(std::unique_ptr<TakeWriter>(
		new TakeWriter(store.root(), pattern, wav, format, std::move(settings), std::move(log.value()))));
}

MediaResult<Done> TakeWriter::append(const std::vector<std::uint8_t> &samples, const CapturedPeriod &period)
{
	if (done_)
		return fail<Done>("MEDIA_TAKE.CLOSED", "Take is already closed", "take_writer.append");
	if (samples.size() != format_.period_bytes())
		return fail<Done>("MEDIA_TAKE.PERIOD_INVALID", "Period has the wrong size", "take_writer.append");
	if (frames_ + format_.period_frames > settings_.maximum_frames)
		return fail<Done>("MEDIA_TAKE.TOO_LONG", "Take reached its maximum length", "take_writer.append");
	auto added = log_.add(period);
	if (!added)
		return added;
	if (!write_all(wav_, samples.data(), samples.size()))
		return fail<Done>("MEDIA_TAKE.WRITE_FAILED", "Cannot write the take audio", "take_writer.append");
	frames_ += format_.period_frames;
	return MediaResult<Done>::success({});
}

MediaResult<TakeSummary> TakeWriter::finish(const StopReason reason)
{
	using Result = MediaResult<TakeSummary>;
	if (done_)
		return fail<TakeSummary>("MEDIA_TAKE.CLOSED", "Take is already closed", "take_writer.finish");
	auto refuse = [this](const char *code, const char *message) {
		abandon();
		return fail<TakeSummary>(code, message, "take_writer.finish");
	};
	if (frames_ == 0)
		return refuse("MEDIA_TAKE.EMPTY", "Take holds no audio");
	if (log_.xruns() != 0)
		return refuse("MEDIA_CAPTURE.XRUN", "Take had an overrun");
	const auto deviation = log_.max_abs_deviation_ns();
	if (deviation && *deviation > settings_.tolerance_ns)
		return refuse("MEDIA_CAPTURE.CONTINUITY_DEVIATION", "Period timestamps deviate beyond the tolerance");
	const auto waiting = log_.periods().back()[0] - frames_;
	if (log_.periods().back()[0] < frames_ || waiting > format_.buffer_frames)
		return refuse("MEDIA_CAPTURE.CONTINUITY_POSITION", "Last hardware position is outside the buffer");

	if (!pwrite_all(wav_, wav_header(format_, frames_), 0) || fsync(wav_) != 0)
		return refuse("MEDIA_TAKE.WRITE_FAILED", "Cannot complete the take audio");
	::close(wav_);
	wav_ = -1;
	const std::string wav_path = directory_ + "/" + kWavName;
	const std::string digest = sha256_file(wav_path);
	if (digest.empty())
		return refuse("MEDIA_TAKE.WRITE_FAILED", "Cannot hash the take audio");
	const std::uint64_t wav_bytes = 44 + frames_ * format_.frame_bytes();
	const std::string take_id = "take-" + digest;

	nlohmann::json periods = nlohmann::json::array();
	for (const auto &[position, timestamp] : log_.periods())
		periods.push_back({position, timestamp});
	const nlohmann::json record = {
		{"schema_version", kTakeRecordSchema},
		{"take_id", take_id},
		{"purpose", "voice_take"},
		{"simulated", format_.simulated},
		{"pcm", format_.pcm},
		{"access", "rw_interleaved"},
		{"format", to_string(format_.format)},
		{"rate", format_.rate},
		{"channels", format_.channels},
		{"period_frames", format_.period_frames},
		{"buffer_frames", format_.buffer_frames},
		{"tstamp_type", to_string(format_.timestamp_type)},
		{"xruns", log_.xruns()},
		{"periods", periods},
		{"tolerance_ns", settings_.tolerance_ns},
		{"max_abs_deviation_ns", deviation ? nlohmann::json(*deviation) : nlohmann::json(nullptr)},
		{"frames", frames_},
		{"wav", {{"name", kWavName}, {"sha256", digest}, {"size_bytes", wav_bytes}}},
		{"started_at", settings_.started_at},
		{"stop_reason", to_string(reason)},
		{"device", settings_.device},
	};
	const std::string text = record.dump() + "\n";
	const int file = ::open((directory_ + "/" + kRecordName).c_str(), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC,
				0600);
	const bool written = file >= 0 &&
			     write_all(file, reinterpret_cast<const std::uint8_t *>(text.data()), text.size()) &&
			     fsync(file) == 0;
	if (file >= 0)
		::close(file);
	if (!written || !fsync_path(directory_, O_RDONLY | O_DIRECTORY))
		return refuse("MEDIA_TAKE.WRITE_FAILED", "Cannot write the take record");
	const std::string final_directory = root_ + "/" + take_id;
	if (access(final_directory.c_str(), F_OK) == 0)
		return refuse("MEDIA_TAKE.DUPLICATE", "An identical take is already stored");
	if (rename(directory_.c_str(), final_directory.c_str()) != 0)
		return refuse("MEDIA_TAKE.WRITE_FAILED", "Cannot commit the take");
	done_ = true;
	if (!fsync_path(root_, O_RDONLY | O_DIRECTORY))
		return fail<TakeSummary>("MEDIA_TAKE.COMMIT_UNCERTAIN", "Take renamed but the store was not synced",
					 "take_writer.finish");
	return Result::success({take_id, reason, frames_, wav_bytes, deviation});
}

void TakeWriter::abandon() noexcept
{
	if (done_)
		return;
	done_ = true;
	if (wav_ >= 0)
		::close(wav_);
	wav_ = -1;
	remove_take_directory(directory_);
}
} // namespace media_broker
