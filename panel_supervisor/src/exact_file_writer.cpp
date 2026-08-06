#include "exact_file_writer.hpp"

#include <cerrno>
#include <cstddef>
#include <fcntl.h>
#include <map>
#include <string>
#include <unistd.h>

namespace house_panel::supervisor::detail {
namespace {

SupervisorError file_write_error(const ErrorCode error_code, const std::string &operation,
				 const std::string &path, const char *failure_stage,
				 const int error_number)
{
	return SupervisorError(
		error_code, "Failed to write the exact configured privileged file value",
		{{"errno", std::to_string(error_number)},
		 {"failure_stage", failure_stage},
		 {"operation", operation},
		 {"path", path}},
		error_number == EINTR || error_number == EAGAIN || error_number == EBUSY);
}

} // namespace

Result<void> write_exact_file(const std::string &path, const std::string &content,
			      const ErrorCode error_code, const std::string &operation)
{
	int open_flags = O_WRONLY | O_TRUNC;
#ifdef O_CLOEXEC
	open_flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
	open_flags |= O_NOFOLLOW;
#endif
	const int file_descriptor = ::open(path.c_str(), open_flags);
	if (file_descriptor < 0) {
		return Result<void>::failure(
			file_write_error(error_code, operation, path, "open", errno));
	}

	std::size_t bytes_written = 0;
	while (bytes_written < content.size()) {
		const ssize_t write_result =
			::write(file_descriptor, content.data() + bytes_written,
				static_cast<std::size_t>(content.size() - bytes_written));
		if (write_result < 0 && errno == EINTR) {
			continue;
		}
		if (write_result <= 0) {
			const int write_error_number = write_result < 0 ? errno : EIO;
			(void)::close(file_descriptor);
			return Result<void>::failure(file_write_error(error_code, operation, path,
								      "write", write_error_number));
		}
		bytes_written += static_cast<std::size_t>(write_result);
	}

	if (::close(file_descriptor) != 0) {
		return Result<void>::failure(
			file_write_error(error_code, operation, path, "close", errno));
	}
	return Result<void>::success();
}

} // namespace house_panel::supervisor::detail
