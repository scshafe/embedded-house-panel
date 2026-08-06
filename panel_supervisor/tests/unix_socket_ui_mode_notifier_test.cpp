#include "fixtures/temporary_directory_fixture.hpp"

#include "panel_supervisor/error_code.hpp"
#include "panel_supervisor/unix_socket_ui_mode_notifier.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>
#include <utility>

namespace house_panel::supervisor {
namespace {

#define TEST_REQUIRE(condition)                                                                    \
	do {                                                                                       \
		if (!(condition)) {                                                                \
			std::cerr << __func__ << ": requirement failed: " #condition << '\n';      \
			return false;                                                              \
		}                                                                                  \
	} while (false)

class ConnectedClient final {
public:
	explicit ConnectedClient(const std::string &socket_path)
	{
		socket_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
		if (socket_ < 0) {
			throw std::runtime_error("failed to create test Unix socket client");
		}
		timeval timeout{2, 0};
		if (::setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, &timeout,
				 static_cast<socklen_t>(sizeof(timeout))) != 0) {
			(void)::close(socket_);
			socket_ = -1;
			throw std::runtime_error("failed to configure test Unix socket timeout");
		}
		sockaddr_un address{};
		address.sun_family = AF_UNIX;
		if (socket_path.size() >= sizeof(address.sun_path)) {
			(void)::close(socket_);
			socket_ = -1;
			throw std::runtime_error("test Unix socket path is too long");
		}
		std::memcpy(address.sun_path, socket_path.c_str(), socket_path.size() + 1);
		if (::connect(socket_, reinterpret_cast<const sockaddr *>(&address),
			      static_cast<socklen_t>(sizeof(address))) != 0) {
			(void)::close(socket_);
			socket_ = -1;
			throw std::runtime_error("failed to connect test Unix socket client");
		}
	}

	~ConnectedClient()
	{
		if (socket_ >= 0) {
			(void)::close(socket_);
		}
	}

	ConnectedClient(const ConnectedClient &) = delete;
	ConnectedClient &operator=(const ConnectedClient &) = delete;

	[[nodiscard]] std::string read_lines(const std::size_t line_count) const
	{
		std::string payload;
		while (static_cast<std::size_t>(std::count(payload.begin(), payload.end(), '\n')) <
		       line_count) {
			char buffer[512]{};
			const ssize_t received = ::recv(socket_, buffer, sizeof(buffer), 0);
			if (received < 0 && errno == EINTR) {
				continue;
			}
			if (received <= 0) {
				throw std::runtime_error(
					"failed to read expected Unix socket lines");
			}
			payload.append(buffer, static_cast<std::size_t>(received));
		}
		return payload;
	}

	[[nodiscard]] bool peer_closed_connection() const
	{
		char byte{};
		while (true) {
			const ssize_t received = ::recv(socket_, &byte, sizeof(byte), 0);
			if (received < 0 && errno == EINTR) {
				continue;
			}
			return received == 0;
		}
	}

private:
	int socket_{-1};
};

int create_bound_socket(const std::string &socket_path, const bool listen_for_clients)
{
	const int socket_descriptor = ::socket(AF_UNIX, SOCK_STREAM, 0);
	if (socket_descriptor < 0) {
		throw std::runtime_error("failed to create bound test Unix socket");
	}
	sockaddr_un address{};
	address.sun_family = AF_UNIX;
	if (socket_path.size() >= sizeof(address.sun_path)) {
		(void)::close(socket_descriptor);
		throw std::runtime_error("bound test Unix socket path is too long");
	}
	std::memcpy(address.sun_path, socket_path.c_str(), socket_path.size() + 1);
	if (::bind(socket_descriptor, reinterpret_cast<const sockaddr *>(&address),
		   static_cast<socklen_t>(sizeof(address))) != 0 ||
	    (listen_for_clients && ::listen(socket_descriptor, 1) != 0)) {
		(void)::close(socket_descriptor);
		throw std::runtime_error("failed to bind or listen on test Unix socket");
	}
	return socket_descriptor;
}

UiModeNotification notification(const UiMode ui_mode, const PresenceState presence_state,
				const SolarState solar_state, const SystemState system_state,
				const int brightness_percent, const long long epoch_seconds)
{
	return UiModeNotification{
		ui_mode,
		presence_state,
		solar_state,
		system_state,
		brightness_percent,
		std::chrono::system_clock::time_point(std::chrono::seconds(epoch_seconds))};
}

bool new_client_receives_current_snapshot_before_incremental_snapshot()
{
	test_fixture::TemporaryDirectory directory;
	const std::string socket_path = directory.child_path("mode.sock");
	{
		auto notifier_result = UnixSocketUiModeNotifier::create(socket_path);
		TEST_REQUIRE(notifier_result.has_value());
		auto notifier = std::move(notifier_result).value();

		struct stat socket_status{};
		TEST_REQUIRE(::stat(socket_path.c_str(), &socket_status) == 0);
		TEST_REQUIRE((socket_status.st_mode & 0777) == 0660);
		TEST_REQUIRE(
			notifier->notify_ui_mode(notification(UiMode::Day, PresenceState::Present,
							      SolarState::Day, SystemState::Active,
							      88, 1'700'000'000LL))
				.has_value());
		TEST_REQUIRE(notifier->event_sequence() == 1);

		ConnectedClient client(socket_path);
		TEST_REQUIRE(notifier->notify_ui_mode(
					     notification(UiMode::Night, PresenceState::GracePeriod,
							  SolarState::Night, SystemState::Active,
							  12, 1'700'000'060LL))
				     .has_value());
		const std::string payload = client.read_lines(2);
		const std::string expected =
			"{\"data\":{\"protocol_version\":1,\"event_sequence\":1,"
			"\"display_mode\":\"vocabulary\",\"presence_state\":\"present\","
			"\"solar_state\":\"day\",\"system_state\":\"active\","
			"\"effective_brightness\":88,"
			"\"observed_at\":\"2023-11-14T22:13:20Z\"},\"error\":null,"
			"\"meta\":{\"degraded\":false,\"degraded_reason\":null,"
			"\"request_id\":\"local-1\"}}\n"
			"{\"data\":{\"protocol_version\":1,\"event_sequence\":2,"
			"\"display_mode\":\"night_red\","
			"\"presence_state\":\"grace_period\",\"solar_state\":\"night\","
			"\"system_state\":\"active\",\"effective_brightness\":12,"
			"\"observed_at\":\"2023-11-14T22:14:20Z\"},\"error\":null,"
			"\"meta\":{\"degraded\":false,\"degraded_reason\":null,"
			"\"request_id\":\"local-2\"}}\n";
		TEST_REQUIRE(payload == expected);
		TEST_REQUIRE(notifier->event_sequence() == 2);
	}
	TEST_REQUIRE(::access(socket_path.c_str(), F_OK) != 0);
	return true;
}

bool existing_path_is_never_replaced_or_removed()
{
	test_fixture::TemporaryDirectory directory;
	directory.write_text("mode.sock", "deployment-owned");
	const std::string socket_path = directory.child_path("mode.sock");
	const auto result = UnixSocketUiModeNotifier::create(socket_path);
	TEST_REQUIRE(!result.has_value());
	TEST_REQUIRE(result.error().code() == ErrorCode::UiSocketBindFailed);
	TEST_REQUIRE(directory.read_text("mode.sock") == "deployment-owned");
	return true;
}

bool extra_client_is_closed_without_disturbing_primary_client()
{
	test_fixture::TemporaryDirectory directory;
	const std::string socket_path = directory.child_path("mode.sock");
	auto notifier_result = UnixSocketUiModeNotifier::create(socket_path);
	TEST_REQUIRE(notifier_result.has_value());
	auto notifier = std::move(notifier_result).value();

	ConnectedClient primary(socket_path);
	TEST_REQUIRE(notifier->notify_ui_mode(notification(UiMode::Day, PresenceState::Present,
							  SolarState::Day, SystemState::Active,
							  80, 1'700'000'000LL))
			     .has_value());
	TEST_REQUIRE(primary.read_lines(1).find("\"event_sequence\":1") != std::string::npos);
	TEST_REQUIRE(notifier->has_connected_client());

	ConnectedClient extra(socket_path);
	TEST_REQUIRE(notifier->notify_ui_mode(notification(UiMode::Night, PresenceState::Present,
							  SolarState::Night, SystemState::Active,
							  10, 1'700'000'060LL))
			     .has_value());
	TEST_REQUIRE(extra.peer_closed_connection());
	TEST_REQUIRE(primary.read_lines(1).find("\"event_sequence\":2") != std::string::npos);
	TEST_REQUIRE(notifier->has_connected_client());
	return true;
}

bool same_owner_stale_socket_is_recovered()
{
	test_fixture::TemporaryDirectory directory;
	const std::string socket_path = directory.child_path("mode.sock");
	const int stale_socket = create_bound_socket(socket_path, false);
	TEST_REQUIRE(::close(stale_socket) == 0);

	{
		auto notifier_result = UnixSocketUiModeNotifier::create(socket_path);
		TEST_REQUIRE(notifier_result.has_value());
		struct stat socket_status {};
		TEST_REQUIRE(::lstat(socket_path.c_str(), &socket_status) == 0);
		TEST_REQUIRE(S_ISSOCK(socket_status.st_mode));
	}
	TEST_REQUIRE(::access(socket_path.c_str(), F_OK) != 0);
	return true;
}

bool active_socket_is_preserved()
{
	test_fixture::TemporaryDirectory directory;
	const std::string socket_path = directory.child_path("mode.sock");
	const int active_socket = create_bound_socket(socket_path, true);
	struct stat before_status {};
	TEST_REQUIRE(::lstat(socket_path.c_str(), &before_status) == 0);

	const auto result = UnixSocketUiModeNotifier::create(socket_path);
	TEST_REQUIRE(!result.has_value());
	TEST_REQUIRE(result.error().code() == ErrorCode::UiSocketBindFailed);
	struct stat after_status {};
	TEST_REQUIRE(::lstat(socket_path.c_str(), &after_status) == 0);
	TEST_REQUIRE(before_status.st_dev == after_status.st_dev);
	TEST_REQUIRE(before_status.st_ino == after_status.st_ino);
	TEST_REQUIRE(::close(active_socket) == 0);
	TEST_REQUIRE(::unlink(socket_path.c_str()) == 0);
	return true;
}

bool invalid_socket_path_returns_configuration_error()
{
	const auto result = UnixSocketUiModeNotifier::create("run/panel-mode.sock");
	TEST_REQUIRE(!result.has_value());
	TEST_REQUIRE(result.error().code() == ErrorCode::UiSocketConfigurationInvalid);
	return true;
}

using TestFunction = bool (*)();

} // namespace
} // namespace house_panel::supervisor

int main()
{
	using namespace house_panel::supervisor;
	const std::array<std::pair<std::string_view, TestFunction>, 6> tests{{
		{"snapshot ordering",
		 new_client_receives_current_snapshot_before_incremental_snapshot},
		{"existing path", existing_path_is_never_replaced_or_removed},
		{"one client", extra_client_is_closed_without_disturbing_primary_client},
		{"stale socket recovery", same_owner_stale_socket_is_recovered},
		{"active socket preservation", active_socket_is_preserved},
		{"invalid path", invalid_socket_path_returns_configuration_error},
	}};
	for (const auto &[name, test] : tests) {
		if (!test()) {
			std::cerr << "FAILED: " << name << '\n';
			return 1;
		}
	}
	std::cout << "unix_socket_ui_mode_notifier_test: " << tests.size() << " passed\n";
	return 0;
}
