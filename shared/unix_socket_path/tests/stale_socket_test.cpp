#include "bhp/unix_socket_path/stale_socket.h"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace {
using bhp::unix_socket_path::StalePath;
using bhp::unix_socket_path::clear_stale_socket;

int failures = 0;

void expect(bool condition, const char *message)
{
	if (!condition) {
		std::cerr << "FAIL: " << message << '\n';
		++failures;
	}
}

bool exists(const std::string &path)
{
	struct stat info {};
	return ::lstat(path.c_str(), &info) == 0;
}

/** Binds `path`; listens when `type` is a stream. Returns the descriptor. */
int bound(const std::string &path, int type)
{
	const int descriptor = ::socket(AF_UNIX, type | SOCK_CLOEXEC, 0);
	sockaddr_un address{};
	address.sun_family = AF_UNIX;
	std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
	if (descriptor < 0 || ::bind(descriptor, reinterpret_cast<sockaddr *>(&address), sizeof address) != 0 ||
	    (type == SOCK_STREAM && ::listen(descriptor, 1) != 0)) {
		std::cerr << "setup: bind " << path << " failed\n";
		std::exit(2);
	}
	return descriptor;
}
} // namespace

int main()
{
	char pattern[] = "/tmp/bhp-stale-socket-XXXXXX";
	const char *made = ::mkdtemp(pattern);
	if (made == nullptr)
		return 2;
	const std::string directory = made;
	const std::string path = directory + "/service.sock";

	expect(clear_stale_socket(path) == StalePath::Absent, "a missing path is absent");

	// A daemon killed without unlinking leaves its socket behind; the next bind used to fail.
	::close(bound(path, SOCK_STREAM));
	expect(clear_stale_socket(path) == StalePath::Removed, "a stale stream socket is removed");
	expect(!exists(path), "the stale stream socket is gone");
	const int restarted = bound(path, SOCK_STREAM);
	expect(clear_stale_socket(path) == StalePath::InUse, "a live listener is in use");
	expect(exists(path), "a live listener's socket stays");
	::close(restarted);
	::unlink(path.c_str());

	::close(bound(path, SOCK_DGRAM));
	expect(clear_stale_socket(path) == StalePath::Removed, "a stale datagram socket is removed");
	const int datagram = bound(path, SOCK_DGRAM);
	expect(clear_stale_socket(path) == StalePath::InUse, "a live datagram socket is in use");
	::close(datagram);
	::unlink(path.c_str());

	std::ofstream(path) << "not a socket";
	expect(clear_stale_socket(path) == StalePath::Foreign, "a regular file is foreign");
	expect(exists(path), "a regular file stays");
	::unlink(path.c_str());

	const std::string target = directory + "/target.sock";
	::close(bound(target, SOCK_STREAM));
	expect(::symlink(target.c_str(), path.c_str()) == 0, "setup: symlink");
	expect(clear_stale_socket(path) == StalePath::Foreign, "a link is never followed");
	expect(exists(path) && exists(target), "neither the link nor its stale target is removed");
	::unlink(path.c_str());
	::unlink(target.c_str());

	expect(clear_stale_socket(std::string(200, 'x')) == StalePath::Failed, "an overlong path fails");
	::rmdir(directory.c_str());
	if (failures == 0)
		std::cout << "stale_socket_test: ok\n";
	return failures == 0 ? 0 : 1;
}
