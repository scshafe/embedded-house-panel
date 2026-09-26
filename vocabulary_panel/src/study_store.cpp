#include "vocabulary_panel/study_store.h"
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
namespace vocabulary_panel
{
namespace
{
VocabularyPanelError io_error(const char *operation, bool uncertain = false)
{
	return {uncertain ? "STUDY.STORAGE_COMMIT_UNCERTAIN" : "STUDY.STORAGE_FAILED",
		"Study storage operation failed",
		{{"operation", operation}, {"errno", std::to_string(errno)}},
		false};
}
bool owned_regular(int fd)
{
	struct stat s{};
	return fstat(fd, &s) == 0 && S_ISREG(s.st_mode) && s.st_uid == geteuid() &&
	       (s.st_mode & 0077) == 0;
}
} // namespace
VocabularyPanelResult<std::optional<std::string>> MemoryStudyStore::read()
{
	return VocabularyPanelResult<std::optional<std::string>>::success(payload_);
}
VocabularyPanelResult<bool> MemoryStudyStore::commit(const std::string &payload)
{
	if (fail_next_commit) {
		fail_next_commit = false;
		return VocabularyPanelResult<bool>::failure(
		    {"STUDY.STORAGE_FAILED", "Injected atomic fixture write failure", {}, false});
	}
	payload_ = payload;
	return VocabularyPanelResult<bool>::success(true);
}
struct FileStudyStore::Implementation {
	int directory{-1}, lock{-1};
	std::size_t limit;
	std::optional<VocabularyPanelError> error;
	bool uncertain{false};
	std::function<void(StudyStoreBoundary)> observe;
	explicit Implementation(std::size_t max) : limit(max) {}
};
FileStudyStore::FileStudyStore(std::string directory, std::size_t maximum_bytes,
			       std::function<void(StudyStoreBoundary)> boundary_observer)
    : implementation_(std::make_unique<Implementation>(maximum_bytes))
{
	auto &s = *implementation_;
	s.observe = std::move(boundary_observer);
	if (maximum_bytes == 0 || maximum_bytes > 64 * 1024 * 1024 || directory.empty()) {
		s.error = VocabularyPanelError{"STUDY.STORAGE_CONFIG_INVALID",
					       "Invalid bounded storage configuration",
					       {},
					       false};
		return;
	}
	if (mkdir(directory.c_str(), 0700) != 0 && errno != EEXIST) {
		s.error = io_error("mkdir");
		return;
	}
	s.directory = open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	struct stat st{};
	if (s.directory < 0 || fstat(s.directory, &st) != 0 || st.st_uid != geteuid() ||
	    (st.st_mode & 0077) != 0) {
		s.error = io_error("owner_only_directory");
		return;
	}
	s.lock = openat(s.directory, "study.lock", O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
	if (s.lock < 0 || !owned_regular(s.lock) || flock(s.lock, LOCK_EX | LOCK_NB) != 0)
		s.error = io_error("exclusive_lock");
}
FileStudyStore::~FileStudyStore()
{
	auto &s = *implementation_;
	if (s.lock >= 0)
		close(s.lock);
	if (s.directory >= 0)
		close(s.directory);
}
VocabularyPanelResult<std::optional<std::string>> FileStudyStore::read()
{
	auto &s = *implementation_;
	using Result = VocabularyPanelResult<std::optional<std::string>>;
	if (s.error)
		return Result::failure(*s.error);
	int fd = openat(s.directory, "study-state.json", O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
	if (fd < 0) {
		if (errno == ENOENT)
			return Result::success(std::nullopt);
		return Result::failure(io_error("open"));
	}
	struct stat st{};
	if (!owned_regular(fd) || fstat(fd, &st) != 0 || st.st_size < 0 ||
	    static_cast<std::uint64_t>(st.st_size) > s.limit) {
		close(fd);
		return Result::failure({"STUDY.STORAGE_INVALID",
					"State file ownership, type or byte bound invalid",
					{},
					false});
	}
	std::string data(static_cast<std::size_t>(st.st_size), '\0');
	std::size_t offset = 0;
	while (offset < data.size()) {
		auto n = ::read(fd, data.data() + offset, data.size() - offset);
		if (n < 0 && errno == EINTR)
			continue;
		if (n <= 0) {
			auto error = io_error("read");
			close(fd);
			return Result::failure(error);
		}
		offset += static_cast<std::size_t>(n);
	}
	close(fd);
	return Result::success(std::move(data));
}
VocabularyPanelResult<bool> FileStudyStore::commit(const std::string &payload)
{
	auto &s = *implementation_;
	using Result = VocabularyPanelResult<bool>;
	if (s.error)
		return Result::failure(*s.error);
	if (s.uncertain)
		return Result::failure(
		    {"STUDY.STORAGE_COMMIT_UNCERTAIN",
		     "Restart required after indeterminate directory synchronization",
		     {},
		     false});
	if (payload.size() > s.limit)
		return Result::failure({"STUDY.STORAGE_FULL",
					"Study state exceeds configured byte ceiling",
					{},
					false});
	// A leftover temp from a crashed writer is never recovered as committed data.
	if (unlinkat(s.directory, "study-state.tmp", 0) != 0 && errno != ENOENT)
		return Result::failure(io_error("remove_uncommitted_temp"));
	int fd = openat(s.directory, "study-state.tmp",
			O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
	if (fd < 0)
		return Result::failure(io_error("open_temp"));
	if (s.observe)
		s.observe(StudyStoreBoundary::temporary_opened);
	std::size_t offset = 0;
	while (offset < payload.size()) {
		auto n = write(fd, payload.data() + offset, payload.size() - offset);
		if (n < 0 && errno == EINTR)
			continue;
		if (n <= 0) {
			auto error = io_error("write_temp");
			close(fd);
			unlinkat(s.directory, "study-state.tmp", 0);
			return Result::failure(error);
		}
		offset += static_cast<std::size_t>(n);
	}
	if (s.observe)
		s.observe(StudyStoreBoundary::payload_written);
	if (fsync(fd) != 0) {
		auto error = io_error("fsync_temp");
		close(fd);
		unlinkat(s.directory, "study-state.tmp", 0);
		return Result::failure(error);
	}
	if (s.observe)
		s.observe(StudyStoreBoundary::file_synchronized);
	if (close(fd) != 0) {
		auto error = io_error("close_temp");
		unlinkat(s.directory, "study-state.tmp", 0);
		return Result::failure(error);
	}
	if (renameat(s.directory, "study-state.tmp", s.directory, "study-state.json") != 0)
		return Result::failure(io_error("rename"));
	if (s.observe)
		s.observe(StudyStoreBoundary::renamed);
	if (fsync(s.directory) != 0) {
		s.uncertain = true;
		return Result::failure(io_error("fsync_directory", true));
	}
	if (s.observe)
		s.observe(StudyStoreBoundary::directory_synchronized);
	return Result::success(true);
}
} // namespace vocabulary_panel
