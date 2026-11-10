#include "bhp/unix_socket_path/stale_socket.h"

#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace bhp::unix_socket_path {

StalePath clear_stale_socket(const std::string &path) noexcept
{
	if (path.empty() || path.size() >= sizeof(sockaddr_un::sun_path))
		return StalePath::Failed;
	struct stat before {};
	if (::lstat(path.c_str(), &before) != 0)
		return errno == ENOENT ? StalePath::Absent : StalePath::Failed;
	if (!S_ISSOCK(before.st_mode) || before.st_uid != ::geteuid())
		return StalePath::Foreign;
	// The probe's type need not match the listener's: a stale path refuses every
	// type with ECONNREFUSED, and a live one of another type answers EPROTOTYPE.
	const int probe = ::socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
	if (probe < 0)
		return StalePath::Failed;
	sockaddr_un address{};
	address.sun_family = AF_UNIX;
	std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
	const int connected = ::connect(probe, reinterpret_cast<const sockaddr *>(&address), sizeof address);
	const int error = connected == 0 ? 0 : errno;
	::close(probe);
	if (error == ENOENT)
		return StalePath::Absent;
	if (error != ECONNREFUSED)
		return StalePath::InUse;
	struct stat after {};
	if (::lstat(path.c_str(), &after) != 0 || after.st_dev != before.st_dev || after.st_ino != before.st_ino)
		return StalePath::Foreign;
	if (::unlink(path.c_str()) != 0)
		return errno == ENOENT ? StalePath::Absent : StalePath::Failed;
	return StalePath::Removed;
}

const char *to_string(StalePath outcome) noexcept
{
	switch (outcome) {
	case StalePath::Absent:
		return "absent";
	case StalePath::Removed:
		return "removed";
	case StalePath::InUse:
		return "in_use";
	case StalePath::Foreign:
		return "foreign";
	case StalePath::Failed:
		break;
	}
	return "failed";
}

} // namespace bhp::unix_socket_path
