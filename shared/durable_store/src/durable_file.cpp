#include "bhp/durable_store/durable_store.h"
#include <cerrno>
#include <cstdint>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
namespace bhp::durable_store
{
namespace
{
bool owned_regular(int fd)
{
	struct stat s{};
	return fstat(fd, &s) == 0 && S_ISREG(s.st_mode) && s.st_uid == geteuid() && (s.st_mode & 0077) == 0;
}
} // namespace

struct DurableFile::Implementation {
	int directory{-1}, lock{-1};
	std::size_t limit;
	DurableFileNames names;
	std::optional<Error> error;
	bool uncertain{false};
	std::function<void(CommitBoundary)> observe;
	Implementation(std::size_t max, DurableFileNames n) : limit(max), names(std::move(n)) {}

	Error io_error(const char *operation, bool commit_uncertain = false) const
	{
		return {names.error_prefix + (commit_uncertain ? ".STORAGE_COMMIT_UNCERTAIN" : ".STORAGE_FAILED"),
			"Durable storage operation failed",
			{{"operation", operation}, {"errno", std::to_string(errno)}}};
	}
	Error error_of(const char *suffix, const char *message) const
	{
		return {names.error_prefix + suffix, message, {}};
	}
	void notify(CommitBoundary boundary) const
	{
		if (observe)
			observe(boundary);
	}
};

DurableFile::DurableFile(std::string directory, DurableFileNames names, const std::size_t maximum_bytes,
			 std::function<void(CommitBoundary)> boundary_observer)
	: implementation_(std::make_unique<Implementation>(maximum_bytes, std::move(names)))
{
	auto &s = *implementation_;
	s.observe = std::move(boundary_observer);
	if (maximum_bytes == 0 || maximum_bytes > 64 * 1024 * 1024 || directory.empty() || s.names.lock.empty() ||
	    s.names.state.empty() || s.names.temporary.empty() || s.names.error_prefix.empty()) {
		s.error = s.error_of(".STORAGE_CONFIG_INVALID", "Invalid bounded storage configuration");
		return;
	}
	if (mkdir(directory.c_str(), 0700) != 0 && errno != EEXIST) {
		s.error = s.io_error("mkdir");
		return;
	}
	s.directory = open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	struct stat st{};
	if (s.directory < 0 || fstat(s.directory, &st) != 0 || st.st_uid != geteuid() || (st.st_mode & 0077) != 0) {
		s.error = s.io_error("owner_only_directory");
		return;
	}
	s.lock = openat(s.directory, s.names.lock.c_str(), O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
	if (s.lock < 0 || !owned_regular(s.lock) || flock(s.lock, LOCK_EX | LOCK_NB) != 0)
		s.error = s.io_error("exclusive_lock");
}

DurableFile::~DurableFile()
{
	auto &s = *implementation_;
	if (s.lock >= 0)
		close(s.lock);
	if (s.directory >= 0)
		close(s.directory);
}

Result<std::optional<std::string>> DurableFile::read()
{
	auto &s = *implementation_;
	using Output = Result<std::optional<std::string>>;
	if (s.error)
		return Output::failure(*s.error);
	int fd = openat(s.directory, s.names.state.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
	if (fd < 0) {
		if (errno == ENOENT)
			return Output::success(std::nullopt);
		return Output::failure(s.io_error("open"));
	}
	struct stat st{};
	if (!owned_regular(fd) || fstat(fd, &st) != 0 || st.st_size < 0 ||
	    static_cast<std::uint64_t>(st.st_size) > s.limit) {
		close(fd);
		return Output::failure(s.error_of(".STORAGE_INVALID", "State file ownership, type or byte bound invalid"));
	}
	std::string data(static_cast<std::size_t>(st.st_size), '\0');
	std::size_t offset = 0;
	while (offset < data.size()) {
		auto n = ::read(fd, data.data() + offset, data.size() - offset);
		if (n < 0 && errno == EINTR)
			continue;
		if (n <= 0) {
			auto error = s.io_error("read");
			close(fd);
			return Output::failure(error);
		}
		offset += static_cast<std::size_t>(n);
	}
	close(fd);
	return Output::success(std::move(data));
}

Result<bool> DurableFile::commit(const std::string &payload)
{
	auto &s = *implementation_;
	using Output = Result<bool>;
	if (s.error)
		return Output::failure(*s.error);
	if (s.uncertain)
		return Output::failure(s.error_of(".STORAGE_COMMIT_UNCERTAIN",
						  "Restart required after indeterminate directory synchronization"));
	if (payload.size() > s.limit)
		return Output::failure(s.error_of(".STORAGE_FULL", "State exceeds the configured byte ceiling"));
	const char *temporary = s.names.temporary.c_str();
	// A leftover temp from a crashed writer is never recovered as committed data.
	if (unlinkat(s.directory, temporary, 0) != 0 && errno != ENOENT)
		return Output::failure(s.io_error("remove_uncommitted_temp"));
	int fd = openat(s.directory, temporary, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
	if (fd < 0)
		return Output::failure(s.io_error("open_temp"));
	s.notify(CommitBoundary::temporary_opened);
	std::size_t offset = 0;
	while (offset < payload.size()) {
		auto n = write(fd, payload.data() + offset, payload.size() - offset);
		if (n < 0 && errno == EINTR)
			continue;
		if (n <= 0) {
			auto error = s.io_error("write_temp");
			close(fd);
			unlinkat(s.directory, temporary, 0);
			return Output::failure(error);
		}
		offset += static_cast<std::size_t>(n);
	}
	s.notify(CommitBoundary::payload_written);
	if (fsync(fd) != 0) {
		auto error = s.io_error("fsync_temp");
		close(fd);
		unlinkat(s.directory, temporary, 0);
		return Output::failure(error);
	}
	s.notify(CommitBoundary::file_synchronized);
	if (close(fd) != 0) {
		auto error = s.io_error("close_temp");
		unlinkat(s.directory, temporary, 0);
		return Output::failure(error);
	}
	if (renameat(s.directory, temporary, s.directory, s.names.state.c_str()) != 0)
		return Output::failure(s.io_error("rename"));
	s.notify(CommitBoundary::renamed);
	if (fsync(s.directory) != 0) {
		s.uncertain = true;
		return Output::failure(s.io_error("fsync_directory", true));
	}
	s.notify(CommitBoundary::directory_synchronized);
	return Output::success(true);
}
} // namespace bhp::durable_store
