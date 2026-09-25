#include "fixtures/temporary_directory_fixture.hpp"

#include "panel_supervisor/error_code.hpp"
#include "panel_supervisor/unix_socket_ui_mode_notifier.hpp"

#include "bhp/panel_mode_protocol/codec.h"

#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <memory>
#include <optional>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>
#include <thread>
#include <utility>
#include <vector>

namespace house_panel::supervisor {
namespace {

namespace protocol = bhp::panel_mode_protocol;
constexpr std::string_view kReleaseId =
	"0.1.0+g0123456789abcdef0123456789abcdef01234567";

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
			throw std::runtime_error("failed to configure test Unix socket timeout");
		}
		sockaddr_un address{};
		address.sun_family = AF_UNIX;
		if (socket_path.size() >= sizeof(address.sun_path)) {
			throw std::runtime_error("test Unix socket path is too long");
		}
		std::memcpy(address.sun_path, socket_path.c_str(), socket_path.size() + 1U);
		if (::connect(socket_, reinterpret_cast<const sockaddr *>(&address),
			      static_cast<socklen_t>(sizeof(address))) != 0) {
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

	[[nodiscard]] std::string read_line() const
	{
		std::string payload;
		while (payload.find('\n') == std::string::npos) {
			std::array<char, 512> buffer{};
			const ssize_t received = ::recv(socket_, buffer.data(), buffer.size(), 0);
			if (received < 0 && errno == EINTR) {
				continue;
			}
			if (received <= 0) {
				throw std::runtime_error("failed to read expected Unix socket line");
			}
			payload.append(buffer.data(), static_cast<std::size_t>(received));
		}
		return payload;
	}

	void send_all(const std::string_view payload) const
	{
		std::size_t sent = 0;
		while (sent < payload.size()) {
			const ssize_t result =
				::send(socket_, payload.data() + sent, payload.size() - sent, 0);
			if (result < 0 && errno == EINTR) {
				continue;
			}
			if (result <= 0) {
				throw std::runtime_error("failed to send test acknowledgement");
			}
			sent += static_cast<std::size_t>(result);
		}
	}

	void disconnect() noexcept
	{
		if (socket_ >= 0) {
			(void)::close(socket_);
			socket_ = -1;
		}
	}

	[[nodiscard]] bool payload_available() const
	{
		pollfd descriptor{socket_, POLLIN, 0};
		return ::poll(&descriptor, 1, 0) > 0 &&
		       (descriptor.revents & POLLIN) != 0;
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
	const int descriptor = ::socket(AF_UNIX, SOCK_STREAM, 0);
	if (descriptor < 0) {
		throw std::runtime_error("failed to create bound test Unix socket");
	}
	sockaddr_un address{};
	address.sun_family = AF_UNIX;
	std::memcpy(address.sun_path, socket_path.c_str(), socket_path.size() + 1U);
	if (::bind(descriptor, reinterpret_cast<const sockaddr *>(&address),
		   static_cast<socklen_t>(sizeof(address))) != 0 ||
	    (listen_for_clients && ::listen(descriptor, 1) != 0)) {
		(void)::close(descriptor);
		throw std::runtime_error("failed to bind or listen on test Unix socket");
	}
	return descriptor;
}

UiModeNotification notification(const UiMode ui_mode, const PresenceState presence,
				const SolarState solar, const SystemState system,
				const int brightness_percent, const long long epoch_milliseconds)
{
	return UiModeNotification{ui_mode, presence, solar, system, brightness_percent,
				  std::chrono::system_clock::time_point(
					  std::chrono::milliseconds(epoch_milliseconds))};
}

std::unique_ptr<UnixSocketUiModeNotifier> create_notifier(
	const std::string &path, const std::uint32_t uid = static_cast<std::uint32_t>(::geteuid()))
{
	auto result = UnixSocketUiModeNotifier::create(path, uid, std::string(kReleaseId));
	if (!result) {
		throw std::runtime_error("failed to create test notifier");
	}
	return std::move(result).value();
}

protocol::PresentationAcknowledgement acknowledgement_for(
	const protocol::Snapshot &snapshot, const std::uint64_t generation,
	const protocol::PresentationResult result = protocol::PresentationResult::Committed)
{
	return protocol::PresentationAcknowledgement{
		snapshot.appliance_release_id, snapshot.supervisor_session_id,
		snapshot.connection_challenge, snapshot.event_sequence, snapshot.snapshot_id,
		snapshot.display_mode, generation, result};
}

bool exact_acknowledgement_commits_and_every_ack_binds_release()
{
	test_fixture::TemporaryDirectory directory;
	auto notifier = create_notifier(directory.child_path("mode.sock"));
	ConnectedClient client(notifier->socket_path());
	auto first_pending = notifier->publish(notification(
		UiMode::Day, PresenceState::Present, SolarState::Day, SystemState::Active, 88,
		1'700'000'000'123LL));
	TEST_REQUIRE(first_pending.has_value());
	TEST_REQUIRE(first_pending.value().expected_commitment.has_value());
	const auto first_snapshot_result = protocol::decode_snapshot(client.read_line());
	TEST_REQUIRE(first_snapshot_result.has_value());
	auto first_ack = acknowledgement_for(first_snapshot_result.value(), 1);
	const auto first_frame = protocol::encode_presentation_acknowledgement(first_ack);
	TEST_REQUIRE(first_frame.has_value());
	client.send_all(std::string_view(first_frame.value()).substr(0, 17));
	client.send_all(std::string_view(first_frame.value()).substr(17));
	const auto first_outcome = notifier->await_presentation(
		first_pending.value(), std::chrono::steady_clock::now() + std::chrono::seconds(1));
	TEST_REQUIRE(std::holds_alternative<PresentationCommitted>(first_outcome));
	const auto &commit = std::get<PresentationCommitted>(first_outcome);
	TEST_REQUIRE(commit.render_generation == 1);
	TEST_REQUIRE(commit.snapshot_id == first_snapshot_result.value().snapshot_id);

	auto second_pending = notifier->publish(notification(
		UiMode::Night, PresenceState::GracePeriod, SolarState::Night,
		SystemState::Active, 12, 1'700'000'000'124LL));
	TEST_REQUIRE(second_pending.has_value());
	const auto second_snapshot_result = protocol::decode_snapshot(client.read_line());
	TEST_REQUIRE(second_snapshot_result.has_value());
	auto second_ack = acknowledgement_for(second_snapshot_result.value(), 2);
	second_ack.appliance_release_id = "0.1.0-different";
	const auto second_frame = protocol::encode_presentation_acknowledgement(second_ack);
	TEST_REQUIRE(second_frame.has_value());
	client.send_all(second_frame.value());
	const auto second_outcome = notifier->await_presentation(
		second_pending.value(), std::chrono::steady_clock::now() + std::chrono::seconds(1));
	TEST_REQUIRE(std::holds_alternative<PresentationNotCommitted>(second_outcome));
	TEST_REQUIRE(std::get<PresentationNotCommitted>(second_outcome).reason ==
		     PresentationNotCommittedReason::Mismatched);
	return true;
}

bool first_release_mismatch_fails_dark_and_closes_connection()
{
	test_fixture::TemporaryDirectory directory;
	auto notifier = create_notifier(directory.child_path("mode.sock"));
	ConnectedClient client(notifier->socket_path());
	auto pending = notifier->publish(notification(
		UiMode::Day, PresenceState::Present, SolarState::Day, SystemState::Active, 80,
		1'700'000'000'000LL));
	const auto snapshot = protocol::decode_snapshot(client.read_line());
	TEST_REQUIRE(pending.has_value() && snapshot.has_value());
	auto ack = acknowledgement_for(snapshot.value(), 1);
	ack.appliance_release_id = "0.1.0-different";
	const auto frame = protocol::encode_presentation_acknowledgement(ack);
	TEST_REQUIRE(frame.has_value());
	client.send_all(frame.value());
	const auto outcome = notifier->await_presentation(
		pending.value(), std::chrono::steady_clock::now() + std::chrono::seconds(1));
	TEST_REQUIRE(std::holds_alternative<PresentationNotCommitted>(outcome));
	TEST_REQUIRE(std::get<PresentationNotCommitted>(outcome).reason ==
		     PresentationNotCommittedReason::Mismatched);
	TEST_REQUIRE(!notifier->has_connected_client());
	TEST_REQUIRE(client.peer_closed_connection());
	return true;
}

bool challenge_mismatch_and_duplicate_generation_are_rejected()
{
	test_fixture::TemporaryDirectory directory;
	auto notifier = create_notifier(directory.child_path("mode.sock"));
	ConnectedClient client(notifier->socket_path());
	auto pending = notifier->publish(notification(
		UiMode::Day, PresenceState::Present, SolarState::Day, SystemState::Active, 80,
		1'700'000'000'000LL));
	const auto snapshot = protocol::decode_snapshot(client.read_line());
	TEST_REQUIRE(pending.has_value() && snapshot.has_value());
	auto ack = acknowledgement_for(snapshot.value(), 1);
	ack.connection_challenge = "00112233445566778899aabbccddeeff";
	const auto frame = protocol::encode_presentation_acknowledgement(ack);
	TEST_REQUIRE(frame.has_value());
	client.send_all(frame.value());
	const auto outcome = notifier->await_presentation(
		pending.value(), std::chrono::steady_clock::now() + std::chrono::seconds(1));
	TEST_REQUIRE(std::holds_alternative<PresentationNotCommitted>(outcome));
	TEST_REQUIRE(std::get<PresentationNotCommitted>(outcome).reason ==
		     PresentationNotCommittedReason::Mismatched);
	return true;
}

bool coalesced_extra_acknowledgement_bytes_fail_closed()
{
	test_fixture::TemporaryDirectory directory;
	auto notifier = create_notifier(directory.child_path("mode.sock"));
	ConnectedClient client(notifier->socket_path());
	auto pending = notifier->publish(notification(
		UiMode::Day, PresenceState::Present, SolarState::Day, SystemState::Active, 80,
		1'700'000'000'000LL));
	const auto snapshot = protocol::decode_snapshot(client.read_line());
	TEST_REQUIRE(pending.has_value() && snapshot.has_value());
	const auto frame = protocol::encode_presentation_acknowledgement(
		acknowledgement_for(snapshot.value(), 1));
	TEST_REQUIRE(frame.has_value());
	client.send_all(frame.value() + "{");
	const auto outcome = notifier->await_presentation(
		pending.value(), std::chrono::steady_clock::now() + std::chrono::seconds(1));
	TEST_REQUIRE(std::holds_alternative<PresentationFailed>(outcome));
	TEST_REQUIRE(!notifier->has_connected_client());
	return true;
}

bool explicit_render_failure_is_degraded_without_claiming_commit()
{
	test_fixture::TemporaryDirectory directory;
	auto notifier = create_notifier(directory.child_path("mode.sock"));
	ConnectedClient client(notifier->socket_path());
	auto pending = notifier->publish(notification(
		UiMode::Day, PresenceState::Present, SolarState::Day, SystemState::Active, 80,
		1'700'000'000'000LL));
	const auto snapshot = protocol::decode_snapshot(client.read_line());
	TEST_REQUIRE(pending.has_value() && snapshot.has_value());
	const auto frame = protocol::encode_presentation_acknowledgement(
		acknowledgement_for(snapshot.value(), 1, protocol::PresentationResult::Failed));
	TEST_REQUIRE(frame.has_value());
	client.send_all(frame.value());
	const auto outcome = notifier->await_presentation(
		pending.value(), std::chrono::steady_clock::now() + std::chrono::seconds(1));
	TEST_REQUIRE(std::holds_alternative<PresentationFailed>(outcome));
	TEST_REQUIRE(std::get<PresentationFailed>(outcome).error.code() ==
		     ErrorCode::UiPresentationExplicitlyFailed);
	TEST_REQUIRE(notifier->has_connected_client());
	return true;
}

bool render_generation_must_advance_within_the_connection()
{
	test_fixture::TemporaryDirectory directory;
	auto notifier = create_notifier(directory.child_path("mode.sock"));
	ConnectedClient client(notifier->socket_path());
	auto first_pending = notifier->publish(notification(
		UiMode::Day, PresenceState::Present, SolarState::Day, SystemState::Active, 80,
		1'700'000'000'000LL));
	const auto first_snapshot = protocol::decode_snapshot(client.read_line());
	TEST_REQUIRE(first_pending.has_value() && first_snapshot.has_value());
	const auto first_frame = protocol::encode_presentation_acknowledgement(
		acknowledgement_for(first_snapshot.value(), 1));
	TEST_REQUIRE(first_frame.has_value());
	client.send_all(first_frame.value());
	TEST_REQUIRE(std::holds_alternative<PresentationCommitted>(
		notifier->await_presentation(first_pending.value(),
					     std::chrono::steady_clock::now() +
						     std::chrono::seconds(1))));

	auto second_pending = notifier->publish(notification(
		UiMode::Night, PresenceState::Present, SolarState::Night, SystemState::Active, 20,
		1'700'000'000'001LL));
	const auto second_snapshot = protocol::decode_snapshot(client.read_line());
	TEST_REQUIRE(second_pending.has_value() && second_snapshot.has_value());
	const auto duplicate_frame = protocol::encode_presentation_acknowledgement(
		acknowledgement_for(second_snapshot.value(), 1));
	TEST_REQUIRE(duplicate_frame.has_value());
	client.send_all(duplicate_frame.value());
	const auto outcome = notifier->await_presentation(
		second_pending.value(),
		std::chrono::steady_clock::now() + std::chrono::seconds(1));
	TEST_REQUIRE(std::holds_alternative<PresentationNotCommitted>(outcome));
	TEST_REQUIRE(std::get<PresentationNotCommitted>(outcome).reason ==
		     PresentationNotCommittedReason::Duplicate);
	return true;
}

bool no_acknowledgement_times_out_and_invalidates_connection()
{
	test_fixture::TemporaryDirectory directory;
	auto notifier = create_notifier(directory.child_path("mode.sock"));
	ConnectedClient client(notifier->socket_path());
	auto pending = notifier->publish(notification(
		UiMode::Day, PresenceState::Present, SolarState::Day, SystemState::Active, 80,
		1'700'000'000'000LL));
	TEST_REQUIRE(pending.has_value());
	TEST_REQUIRE(protocol::decode_snapshot(client.read_line()).has_value());
	const auto outcome = notifier->await_presentation(
		pending.value(), std::chrono::steady_clock::now() + std::chrono::milliseconds(5));
	TEST_REQUIRE(std::holds_alternative<PresentationNotCommitted>(outcome));
	TEST_REQUIRE(std::get<PresentationNotCommitted>(outcome).reason ==
		     PresentationNotCommittedReason::TimedOut);
	TEST_REQUIRE(!notifier->has_connected_client());
	return true;
}

bool ui_disconnect_before_acknowledgement_fails_dark_immediately()
{
	test_fixture::TemporaryDirectory directory;
	auto notifier = create_notifier(directory.child_path("mode.sock"));
	ConnectedClient client(notifier->socket_path());
	auto pending = notifier->publish(notification(
		UiMode::Day, PresenceState::Present, SolarState::Day, SystemState::Active, 80,
		1'700'000'000'000LL));
	TEST_REQUIRE(pending.has_value());
	TEST_REQUIRE(protocol::decode_snapshot(client.read_line()).has_value());
	client.disconnect();
	const auto outcome = notifier->await_presentation(
		pending.value(), std::chrono::steady_clock::now() + std::chrono::seconds(1));
	TEST_REQUIRE(std::holds_alternative<PresentationNotCommitted>(outcome));
	TEST_REQUIRE(std::get<PresentationNotCommitted>(outcome).reason ==
		     PresentationNotCommittedReason::Disconnected);
	TEST_REQUIRE(!notifier->has_connected_client());
	return true;
}

bool supervisor_restart_invalidates_prior_session_acknowledgement()
{
	test_fixture::TemporaryDirectory directory;
	const std::string socket_path = directory.child_path("mode.sock");
	std::string prior_acknowledgement;
	std::string prior_session;
	{
		auto notifier = create_notifier(socket_path);
		ConnectedClient client(notifier->socket_path());
		auto pending = notifier->publish(notification(
			UiMode::Day, PresenceState::Present, SolarState::Day,
			SystemState::Active, 80, 1'700'000'000'000LL));
		TEST_REQUIRE(pending.has_value());
		const auto snapshot = protocol::decode_snapshot(client.read_line());
		TEST_REQUIRE(snapshot.has_value());
		prior_session = snapshot.value().supervisor_session_id;
		const auto frame = protocol::encode_presentation_acknowledgement(
			acknowledgement_for(snapshot.value(), 1));
		TEST_REQUIRE(frame.has_value());
		prior_acknowledgement = frame.value();
	}

	auto restarted_notifier = create_notifier(socket_path);
	ConnectedClient restarted_client(restarted_notifier->socket_path());
	auto restarted_pending = restarted_notifier->publish(notification(
		UiMode::Day, PresenceState::Present, SolarState::Day, SystemState::Active, 80,
		1'700'000'000'001LL));
	TEST_REQUIRE(restarted_pending.has_value());
	const auto restarted_snapshot =
		protocol::decode_snapshot(restarted_client.read_line());
	TEST_REQUIRE(restarted_snapshot.has_value());
	TEST_REQUIRE(restarted_snapshot.value().supervisor_session_id != prior_session);
	restarted_client.send_all(prior_acknowledgement);
	const auto outcome = restarted_notifier->await_presentation(
		restarted_pending.value(),
		std::chrono::steady_clock::now() + std::chrono::seconds(1));
	TEST_REQUIRE(std::holds_alternative<PresentationNotCommitted>(outcome));
	TEST_REQUIRE(std::get<PresentationNotCommitted>(outcome).reason ==
		     PresentationNotCommittedReason::Mismatched);
	TEST_REQUIRE(!restarted_notifier->has_connected_client());
	return true;
}

bool wrong_uid_client_is_closed_without_receiving_a_snapshot()
{
	test_fixture::TemporaryDirectory directory;
	const auto wrong_uid = static_cast<std::uint32_t>(::geteuid()) + 1U;
	auto notifier = create_notifier(directory.child_path("mode.sock"), wrong_uid);
	ConnectedClient client(notifier->socket_path());
	auto pending = notifier->publish(notification(
		UiMode::Day, PresenceState::Present, SolarState::Day, SystemState::Active, 80,
		1'700'000'000'000LL));
	TEST_REQUIRE(pending.has_value());
	TEST_REQUIRE(!notifier->has_connected_client());
	TEST_REQUIRE(client.peer_closed_connection());
	return true;
}

bool continuously_replenished_connection_backlog_cannot_monopolize_policy()
{
	test_fixture::TemporaryDirectory directory;
	auto notifier = create_notifier(directory.child_path("mode.sock"));
	TEST_REQUIRE(UnixSocketUiModeNotifier::maximum_accept_attempts_per_pass() == 4U);

	std::atomic<bool> stop_flood{false};
	std::vector<std::thread> flooders;
	for (std::size_t index = 0U; index < 4U; ++index) {
		flooders.emplace_back([&]() {
			while (!stop_flood.load(std::memory_order_acquire)) {
				const int socket_descriptor = ::socket(AF_UNIX, SOCK_STREAM, 0);
				if (socket_descriptor < 0) {
					continue;
				}
				const int flags = ::fcntl(socket_descriptor, F_GETFL, 0);
				if (flags >= 0) {
					(void)::fcntl(socket_descriptor, F_SETFL, flags | O_NONBLOCK);
				}
				sockaddr_un address{};
				address.sun_family = AF_UNIX;
				std::memcpy(address.sun_path, notifier->socket_path().c_str(),
					    notifier->socket_path().size() + 1U);
				(void)::connect(
					socket_descriptor,
					reinterpret_cast<const sockaddr *>(&address),
					static_cast<socklen_t>(sizeof(address)));
				(void)::close(socket_descriptor);
				std::this_thread::yield();
			}
		});
	}
	std::this_thread::sleep_for(std::chrono::milliseconds(10));

	const auto publish_started = std::chrono::steady_clock::now();
	auto pending = notifier->publish(notification(
		UiMode::Day, PresenceState::Present, SolarState::Day, SystemState::Active, 80,
		1'700'000'000'000LL));
	const auto publish_elapsed = std::chrono::steady_clock::now() - publish_started;
	std::optional<PresentationOutcome> outcome;
	std::chrono::steady_clock::duration await_elapsed{};
	if (pending) {
		const auto await_started = std::chrono::steady_clock::now();
		outcome.emplace(notifier->await_presentation(
			pending.value(), await_started + std::chrono::milliseconds(25)));
		await_elapsed = std::chrono::steady_clock::now() - await_started;
	}
	stop_flood.store(true, std::memory_order_release);
	for (auto &flooder : flooders) {
		flooder.join();
	}

	TEST_REQUIRE(pending);
	TEST_REQUIRE(publish_elapsed < std::chrono::milliseconds(500));
	TEST_REQUIRE(await_elapsed < std::chrono::milliseconds(500));
	TEST_REQUIRE(outcome.has_value());
	TEST_REQUIRE(!std::holds_alternative<PresentationCommitted>(*outcome));
	return true;
}

bool late_connection_cannot_amend_or_satisfy_an_existing_publication()
{
	test_fixture::TemporaryDirectory directory;
	auto notifier = create_notifier(directory.child_path("mode.sock"));
	auto first_pending = notifier->publish(notification(
		UiMode::Day, PresenceState::Present, SolarState::Day, SystemState::Active, 80,
		1'700'000'000'000LL));
	TEST_REQUIRE(first_pending);
	TEST_REQUIRE(!first_pending.value().expected_commitment.has_value());

	ConnectedClient client(notifier->socket_path());
	const auto first_outcome = notifier->await_presentation(
		first_pending.value(), std::chrono::steady_clock::now() +
					       std::chrono::seconds(1));
	TEST_REQUIRE(std::holds_alternative<PresentationNotCommitted>(first_outcome));
	TEST_REQUIRE(std::get<PresentationNotCommitted>(first_outcome).reason ==
		     PresentationNotCommittedReason::AcknowledgementUnavailable);
	TEST_REQUIRE(!client.payload_available());

	auto second_pending = notifier->publish(notification(
		UiMode::Day, PresenceState::Present, SolarState::Day, SystemState::Active, 80,
		1'700'000'000'001LL));
	TEST_REQUIRE(second_pending);
	TEST_REQUIRE(second_pending.value().expected_commitment.has_value());
	const auto snapshot = protocol::decode_snapshot(client.read_line());
	TEST_REQUIRE(snapshot);
	const auto acknowledgement = protocol::encode_presentation_acknowledgement(
		acknowledgement_for(snapshot.value(), 1U));
	TEST_REQUIRE(acknowledgement);
	client.send_all(acknowledgement.value());
	const auto second_outcome = notifier->await_presentation(
		second_pending.value(), std::chrono::steady_clock::now() +
						std::chrono::seconds(1));
	TEST_REQUIRE(std::holds_alternative<PresentationCommitted>(second_outcome));
	return true;
}

bool zero_client_publication_reports_acknowledgement_unavailable_without_waiting()
{
	test_fixture::TemporaryDirectory directory;
	auto notifier = create_notifier(directory.child_path("mode.sock"));
	auto pending = notifier->publish(notification(
		UiMode::Day, PresenceState::Present, SolarState::Day, SystemState::Active, 80,
		1'700'000'000'000LL));
	TEST_REQUIRE(pending);
	TEST_REQUIRE(!pending.value().expected_commitment.has_value());
	const auto await_started = std::chrono::steady_clock::now();
	const auto outcome = notifier->await_presentation(
		pending.value(), await_started + std::chrono::seconds(2));
	const auto await_elapsed = std::chrono::steady_clock::now() - await_started;
	TEST_REQUIRE(std::holds_alternative<PresentationNotCommitted>(outcome));
	TEST_REQUIRE(std::get<PresentationNotCommitted>(outcome).reason ==
		     PresentationNotCommittedReason::AcknowledgementUnavailable);
	TEST_REQUIRE(await_elapsed < std::chrono::milliseconds(500));
	return true;
}

bool existing_path_is_never_replaced_or_removed()
{
	test_fixture::TemporaryDirectory directory;
	directory.write_text("mode.sock", "deployment-owned");
	const std::string path = directory.child_path("mode.sock");
	const auto result = UnixSocketUiModeNotifier::create(
		path, static_cast<std::uint32_t>(::geteuid()), std::string(kReleaseId));
	TEST_REQUIRE(!result.has_value());
	TEST_REQUIRE(result.error().code() == ErrorCode::UiSocketBindFailed);
	TEST_REQUIRE(directory.read_text("mode.sock") == "deployment-owned");
	return true;
}

bool same_owner_stale_socket_is_recovered()
{
	test_fixture::TemporaryDirectory directory;
	const std::string path = directory.child_path("mode.sock");
	const int stale_socket = create_bound_socket(path, false);
	TEST_REQUIRE(::close(stale_socket) == 0);
	{
		auto notifier = create_notifier(path);
		struct stat status {};
		TEST_REQUIRE(::lstat(path.c_str(), &status) == 0 && S_ISSOCK(status.st_mode));
	}
	TEST_REQUIRE(::access(path.c_str(), F_OK) != 0);
	return true;
}

bool active_socket_is_preserved()
{
	test_fixture::TemporaryDirectory directory;
	const std::string path = directory.child_path("mode.sock");
	const int active_socket = create_bound_socket(path, true);
	struct stat before {};
	TEST_REQUIRE(::lstat(path.c_str(), &before) == 0);
	const auto result = UnixSocketUiModeNotifier::create(
		path, static_cast<std::uint32_t>(::geteuid()), std::string(kReleaseId));
	TEST_REQUIRE(!result.has_value());
	struct stat after {};
	TEST_REQUIRE(::lstat(path.c_str(), &after) == 0);
	TEST_REQUIRE(before.st_dev == after.st_dev && before.st_ino == after.st_ino);
	TEST_REQUIRE(::close(active_socket) == 0);
	TEST_REQUIRE(::unlink(path.c_str()) == 0);
	return true;
}

bool invalid_configuration_returns_before_creating_a_path()
{
	const auto result = UnixSocketUiModeNotifier::create(
		"run/panel-mode.sock", 0, std::string(kReleaseId));
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
	const std::array<std::pair<std::string_view, TestFunction>, 17> tests{{
		{"exact acknowledgement",
		 exact_acknowledgement_commits_and_every_ack_binds_release},
		{"release mismatch", first_release_mismatch_fails_dark_and_closes_connection},
		{"challenge mismatch", challenge_mismatch_and_duplicate_generation_are_rejected},
		{"coalesced extra acknowledgement",
		 coalesced_extra_acknowledgement_bytes_fail_closed},
		{"explicit render failure",
		 explicit_render_failure_is_degraded_without_claiming_commit},
		{"render generation", render_generation_must_advance_within_the_connection},
		{"timeout", no_acknowledgement_times_out_and_invalidates_connection},
		{"UI disconnect", ui_disconnect_before_acknowledgement_fails_dark_immediately},
		{"supervisor restart",
		 supervisor_restart_invalidates_prior_session_acknowledgement},
		{"peer UID", wrong_uid_client_is_closed_without_receiving_a_snapshot},
		{"connection flood budget",
		 continuously_replenished_connection_backlog_cannot_monopolize_policy},
		{"late connection publication isolation",
		 late_connection_cannot_amend_or_satisfy_an_existing_publication},
		{"zero-client publication",
		 zero_client_publication_reports_acknowledgement_unavailable_without_waiting},
		{"existing path", existing_path_is_never_replaced_or_removed},
		{"stale socket recovery", same_owner_stale_socket_is_recovered},
		{"active socket preservation", active_socket_is_preserved},
		{"invalid configuration", invalid_configuration_returns_before_creating_a_path},
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
