// bhp-log-tee: copies a service's stderr to its own stdout, line by line, and
// to the bug reporter's ingest socket (bug-reports-and-self-repair.md).
//   bhp-log-tee --socket PATH --service NAME
// The init script runs the service as `SERVICE 2>&1 | bhp-log-tee ...`. The
// copy to stdout never waits on the reporter: when the reporter is away or
// slow, lines for it are dropped and the tee reconnects every 5 s.
//   bhp-log-tee --socket PATH --service NAME --exit-status N [--signal S] [--restarting]
// The restart wrapper's call when a service ended without being asked to.
#include "bug_reporter/ingest.h"
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <iostream>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace
{
using namespace bug_reporter;
const char *kUsage = "usage: bhp-log-tee --socket PATH --service NAME\n"
		     "       bhp-log-tee --socket PATH --service NAME --exit-status N [--signal S] [--restarting]\n";

int connect_to(const std::string &path)
{
	if (path.empty() || path[0] != '/' || path.size() >= sizeof(sockaddr_un::sun_path))
		return -1;
	const int descriptor = ::socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
	sockaddr_un address{};
	address.sun_family = AF_UNIX;
	std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
	if (descriptor >= 0 && ::connect(descriptor, reinterpret_cast<sockaddr *>(&address), sizeof address) == 0)
		return descriptor;
	if (descriptor >= 0)
		::close(descriptor);
	return -1;
}

/** One whole line or nothing: a partial send would corrupt the stream, so it closes. */
bool send_line(int &descriptor, const std::string &line)
{
	if (descriptor < 0)
		return false;
	const auto text = line + "\n";
	const auto sent = ::send(descriptor, text.data(), text.size(), MSG_NOSIGNAL | MSG_DONTWAIT);
	if (sent == static_cast<ssize_t>(text.size()))
		return true;
	if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
		return false; // the reporter is behind: drop this line, keep the stream
	::close(descriptor);
	descriptor = -1;
	return false;
}

bool number(const std::string &text, int maximum, int &out)
{
	if (text.empty() || text.size() > 3 || text.find_first_not_of("0123456789") != std::string::npos)
		return false;
	out = std::stoi(text);
	return out <= maximum;
}
} // namespace

int main(int argc, char **argv)
{
	std::string socket_path, service;
	ExitLine exit;
	bool exit_mode = false;
	for (int at = 1; at < argc; ++at) {
		const std::string option = argv[at];
		if (option == "--restarting") {
			exit.restarting = true;
			continue;
		}
		if (at + 1 >= argc) {
			std::cerr << kUsage;
			return 2;
		}
		const std::string value = argv[++at];
		bool ok = true;
		if (option == "--socket")
			socket_path = value;
		else if (option == "--service")
			service = value;
		else if (option == "--exit-status")
			ok = exit_mode = number(value, 255, exit.status);
		else if (option == "--signal")
			ok = number(value, 64, exit.signal);
		else
			ok = false;
		if (!ok) {
			std::cerr << kUsage;
			return 2;
		}
	}
	if (socket_path.empty() || !valid_service(service)) {
		std::cerr << kUsage;
		return 2;
	}
	std::signal(SIGPIPE, SIG_IGN);
	if (exit_mode) {
		exit.service = service;
		int descriptor = connect_to(socket_path);
		const bool sent = send_line(descriptor, encode_hello(service)) && send_line(descriptor, encode_exit(exit));
		if (descriptor >= 0)
			::close(descriptor);
		return sent ? 0 : 1;
	}
	int descriptor = connect_to(socket_path);
	if (descriptor >= 0 && !send_line(descriptor, encode_hello(service)) && descriptor >= 0) {
		::close(descriptor);
		descriptor = -1;
	}
	auto retry_at = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	for (std::string line; std::getline(std::cin, line);) {
		std::cout << line << '\n' << std::flush;
		if (descriptor < 0 && std::chrono::steady_clock::now() >= retry_at) {
			descriptor = connect_to(socket_path);
			if (descriptor >= 0 && !send_line(descriptor, encode_hello(service)) && descriptor >= 0) {
				::close(descriptor);
				descriptor = -1;
			}
			retry_at = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		}
		(void)send_line(descriptor, line);
	}
	if (descriptor >= 0)
		::close(descriptor);
	return 0;
}
