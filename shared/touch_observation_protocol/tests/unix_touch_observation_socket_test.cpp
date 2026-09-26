#include "bhp/touch_observation_protocol/unix_touch_observation_socket.h"
#include <iostream>
#include <chrono>
#include <cstring>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
using namespace bhp::touch_observation_protocol;
#define REQUIRE(c) do { if (!(c)) { std::cerr << __LINE__ << ": " #c "\n"; return 1; } } while (false)
namespace {
#if defined(__linux__)
NormalizedTouchObservation fixture(std::uint64_t sequence) {
	return {1, "fixture-observation-000001", sequence, {2, std::string(32, 'a'), 2,
		"sha256:" + std::string(64, 'b'), std::string(32, 'c'), 3}, -100, 200, ContactState::Completed, 150};
}
int raw_connect(const std::string &path) {
	int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0); if (fd < 0) return -1;
	sockaddr_un address{}; address.sun_family = AF_UNIX; std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
	if (::connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0) { ::close(fd); return -1; }
	return fd;
}
int child(const std::string &path, int mode, int status, int instruction) {
	if (::setgid(65534) != 0 || ::setuid(65534) != 0) return 20;
	if (mode == 0) {
		auto client = UnixTouchObservationClient::connect(path, 0); if (!client) return 21;
		if (::write(status, "c", 1) != 1) return 22;
		for (int attempt = 0; attempt < 1000; ++attempt) {
			auto received = client.value()->try_receive();
			if (!received) return 23;
			if (received.value()) return received.value()->raw_axis_x == -100 ? 0 : 24;
			::usleep(1000);
		}
		return 25;
	}
	int fd = raw_connect(path); if (fd < 0) return 26;
	if (::write(status, "c", 1) != 1) return 27;
	char command{};
	if (::read(instruction, &command, 1) != 1) return 28;
	if (mode == 1) {
		if (::write(fd, "x", 1) != 1 || ::write(status, "w", 1) != 1) return 29;
		if (::read(instruction, &command, 1) != 1) return 30;
	}
	::close(fd); return 0;
}
#endif
}
int main() {
#if defined(__linux__)
	REQUIRE(!UnixTouchObservationServer::create("/tmp/touch-untrusted.sock", 65534));
	if (::geteuid() != 0) {
		std::cout << "{\"test\":\"unix_touch_observation_socket\",\"result\":\"requires_linux_root_for_uid_fixtures\"}\n"; return 0;
	}
	const auto directory = "/run/bhp-touch-test-" + std::to_string(::getpid());
	REQUIRE(::mkdir(directory.c_str(), 0755) == 0);
	const auto path = directory + "/touch.sock";
	{
		auto server = UnixTouchObservationServer::create(path, 65534); REQUIRE(server);
		REQUIRE(::chown(path.c_str(), 0, 65534) == 0);
		REQUIRE(!UnixTouchObservationClient::connect(path, 65534));
		// A valid path is insufficient: server rejects the wrong kernel UID (root).
		int wrong = raw_connect(path); REQUIRE(wrong >= 0); REQUIRE(server.value()->pump());
		REQUIRE(!server.value()->connected()); ::close(wrong);
		for (int mode = 0; mode < 3; ++mode) {
			int status[2]{}, instruction[2]{}; REQUIRE(::pipe(status) == 0 && ::pipe(instruction) == 0);
			pid_t pid = ::fork(); REQUIRE(pid >= 0);
			if (pid == 0) { ::close(status[0]); ::close(instruction[1]); _exit(child(path, mode, status[1], instruction[0])); }
			::close(status[1]); ::close(instruction[0]); char byte{};
			REQUIRE(::read(status[0], &byte, 1) == 1); REQUIRE(server.value()->pump()); REQUIRE(server.value()->connected());
			if (mode == 0) REQUIRE(server.value()->try_publish(fixture(1)));
			if (mode == 1) {
				REQUIRE(::write(instruction[1], "w", 1) == 1);
				REQUIRE(::read(status[0], &byte, 1) == 1);
				REQUIRE(!server.value()->pump()); REQUIRE(!server.value()->connected());
			}
			if (mode == 2) {
				bool dropped = false; const auto before = std::chrono::steady_clock::now();
				for (std::uint64_t i = 1; i < 1000; ++i) {
					if (!server.value()->try_publish(fixture(i))) { dropped = true; break; }
				}
				REQUIRE(dropped && !server.value()->connected());
				REQUIRE(std::chrono::steady_clock::now() - before < std::chrono::seconds(1));
			}
			if (mode != 0) REQUIRE(::write(instruction[1], "q", 1) == 1);
			int exit{}; REQUIRE(::waitpid(pid, &exit, 0) == pid); REQUIRE(WIFEXITED(exit) && WEXITSTATUS(exit) == 0);
			if (mode == 0) REQUIRE(!server.value()->pump());
			::close(status[0]); ::close(instruction[1]);
		}
	}
	REQUIRE(::rmdir(directory.c_str()) == 0);
	std::cout << "{\"test\":\"unix_touch_observation_socket\",\"result\":\"passed\",\"evidence\":\"linux_software_only\"}\n";
#else
	REQUIRE(!UnixTouchObservationServer::create("/run/touch.sock", 65534));
	REQUIRE(!UnixTouchObservationClient::connect("/run/touch.sock", 0));
	std::cout << "{\"test\":\"unix_touch_observation_socket\",\"result\":\"unsupported_platform_rejected\"}\n";
#endif
}
