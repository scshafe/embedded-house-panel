#include "panel_ui/unix_socket_lifecycle_client.hpp"

#include "bhp/application_identity.h"
#include "bhp/panel_mode_protocol/codec.h"
#include "bhp/panel_mode_protocol/types.h"

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>
#include <utility>

namespace house_panel::panel_ui {
namespace {

namespace protocol = bhp::panel_mode_protocol;

constexpr std::string_view kSessionA = "00112233445566778899aabbccddeeff";
constexpr std::string_view kSessionB = "102132435465768798a9bacbdcedfe0f";
constexpr std::string_view kChallengeA = "ffeeddccbbaa99887766554433221100";
constexpr std::string_view kChallengeB = "0ffedccbbaa998877665544332211020";

#define TEST_REQUIRE(condition)                                                                    \
	do {                                                                                       \
		if (!(condition)) {                                                                \
			std::cerr << __func__ << ": requirement failed: " #condition << '\n';      \
			return false;                                                              \
		}                                                                                  \
	} while (false)

class TemporaryLifecycleServer final {
public:
	TemporaryLifecycleServer()
	{
		std::array<char, 64> directory_template{};
		const std::string pattern = "/tmp/bhp-panel-ui-client-XXXXXX";
		std::memcpy(directory_template.data(), pattern.c_str(), pattern.size() + 1U);
		char *created = ::mkdtemp(directory_template.data());
		if (created == nullptr) {
			throw std::runtime_error("failed to create lifecycle socket test directory");
		}
		directory_path_ = created;
		socket_path_ = directory_path_ + "/mode.sock";

		listening_socket_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
		if (listening_socket_ < 0) {
			throw std::runtime_error("failed to create lifecycle test server socket");
		}
		sockaddr_un address{};
		address.sun_family = AF_UNIX;
		if (socket_path_.size() >= sizeof(address.sun_path)) {
			throw std::runtime_error("lifecycle test socket path is too long");
		}
		std::memcpy(address.sun_path, socket_path_.c_str(), socket_path_.size() + 1U);
		if (::bind(listening_socket_, reinterpret_cast<const sockaddr *>(&address),
			   static_cast<socklen_t>(sizeof(address))) != 0 ||
		    ::listen(listening_socket_, 1) != 0) {
			throw std::runtime_error("failed to bind lifecycle test server socket");
		}
	}

	~TemporaryLifecycleServer()
	{
		close_client();
		if (listening_socket_ >= 0) {
			(void)::close(listening_socket_);
		}
		if (!socket_path_.empty()) {
			(void)::unlink(socket_path_.c_str());
		}
		if (!directory_path_.empty()) {
			(void)::rmdir(directory_path_.c_str());
		}
	}

	TemporaryLifecycleServer(const TemporaryLifecycleServer &) = delete;
	TemporaryLifecycleServer &operator=(const TemporaryLifecycleServer &) = delete;

	[[nodiscard]] const std::string &socket_path() const noexcept { return socket_path_; }

	void accept_client()
	{
		client_socket_ = ::accept(listening_socket_, nullptr, nullptr);
		if (client_socket_ < 0) {
			throw std::runtime_error("failed to accept lifecycle test client");
		}
#if defined(SO_NOSIGPIPE)
		const int enabled = 1;
		(void)::setsockopt(client_socket_, SOL_SOCKET, SO_NOSIGPIPE, &enabled,
				   static_cast<socklen_t>(sizeof(enabled)));
#endif
	}

	void send_all(const std::string_view payload) const
	{
		std::size_t sent = 0;
		while (sent < payload.size()) {
#if defined(MSG_NOSIGNAL)
			constexpr int flags = MSG_NOSIGNAL;
#else
			constexpr int flags = 0;
#endif
			const ssize_t result = ::send(client_socket_, payload.data() + sent,
						      payload.size() - sent, flags);
			if (result < 0 && errno == EINTR) {
				continue;
			}
			if (result <= 0) {
				throw std::runtime_error("failed to send lifecycle test payload");
			}
			sent += static_cast<std::size_t>(result);
		}
	}

	[[nodiscard]] std::string read_line() const
	{
		std::string payload;
		while (payload.find('\n') == std::string::npos) {
			pollfd descriptor{client_socket_, POLLIN, 0};
			if (::poll(&descriptor, 1, 2000) <= 0 ||
			    (descriptor.revents & POLLIN) == 0) {
				throw std::runtime_error("timed out receiving lifecycle test line");
			}
			std::array<char, 512> buffer{};
			const ssize_t received =
				::recv(client_socket_, buffer.data(), buffer.size(), 0);
			if (received < 0 && errno == EINTR) {
				continue;
			}
			if (received <= 0) {
				throw std::runtime_error("failed to receive lifecycle test line");
			}
			payload.append(buffer.data(), static_cast<std::size_t>(received));
		}
		return payload;
	}

	[[nodiscard]] bool payload_available() const
	{
		pollfd descriptor{client_socket_, POLLIN, 0};
		if (::poll(&descriptor, 1, 0) <= 0 || (descriptor.revents & POLLIN) == 0) {
			return false;
		}
		char byte{};
		return ::recv(client_socket_, &byte, sizeof(byte), MSG_PEEK) > 0;
	}

	[[nodiscard]] bool client_closed() const
	{
		pollfd descriptor{client_socket_, POLLIN, 0};
		if (::poll(&descriptor, 1, 1000) <= 0) {
			return false;
		}
		char byte{};
		while (true) {
			const ssize_t received = ::recv(client_socket_, &byte, sizeof(byte), 0);
			if (received < 0 && errno == EINTR) {
				continue;
			}
			return received == 0;
		}
	}

	void close_client() noexcept
	{
		if (client_socket_ >= 0) {
			(void)::close(client_socket_);
			client_socket_ = -1;
		}
	}

private:
	std::string directory_path_;
	std::string socket_path_;
	int listening_socket_{-1};
	int client_socket_{-1};
};

protocol::Snapshot snapshot_fixture(std::string session_id, std::string challenge,
				    const std::uint64_t sequence,
				    const protocol::DisplayMode display_mode =
					    protocol::DisplayMode::DayActive,
				    std::string release_id = std::string(bhp::kApplianceReleaseId))
{
	protocol::Snapshot snapshot{
		std::move(release_id),
		std::move(session_id),
		std::move(challenge),
		sequence,
		{},
		display_mode,
		protocol::PresenceState::Present,
		protocol::SolarState::Day,
		protocol::SystemState::Active,
		80,
		1'700'000'000'123LL + static_cast<std::int64_t>(sequence),
	};
	const auto digest = protocol::compute_snapshot_id(snapshot);
	if (!digest) {
		throw std::runtime_error("failed to digest lifecycle test snapshot");
	}
	snapshot.snapshot_id = digest.value();
	return snapshot;
}

std::string encode_snapshot_fixture(const protocol::Snapshot &snapshot)
{
	const auto encoded = protocol::encode_snapshot(snapshot);
	if (!encoded) {
		throw std::runtime_error("failed to encode lifecycle test snapshot");
	}
	return encoded.value();
}

std::unique_ptr<UnixSocketLifecycleClient>
connect_client(TemporaryLifecycleServer &server)
{
	auto client_result = UnixSocketLifecycleClient::connect(
		server.socket_path(), static_cast<std::uint32_t>(::geteuid()));
	if (!client_result) {
		throw std::runtime_error("failed to connect lifecycle test client");
	}
	server.accept_client();
	return std::move(client_result).value();
}

std::string expected_acknowledgement(const protocol::Snapshot &snapshot,
				     const LifecycleRenderCommit &commit)
{
	const protocol::PresentationAcknowledgement acknowledgement{
		std::string(bhp::kApplianceReleaseId), snapshot.supervisor_session_id,
		snapshot.connection_challenge, snapshot.event_sequence, snapshot.snapshot_id,
		snapshot.display_mode, commit.render_generation, commit.presentation_result};
	const auto encoded = protocol::encode_presentation_acknowledgement(acknowledgement);
	if (!encoded) {
		throw std::runtime_error("failed to encode lifecycle test acknowledgement");
	}
	return encoded.value();
}

bool fragmented_snapshot_is_committed_only_after_callback_and_ack_is_exact()
{
	TemporaryLifecycleServer server;
	auto client = connect_client(server);
	const auto snapshot = snapshot_fixture(std::string(kSessionA), std::string(kChallengeA), 42);
	const std::string frame = encode_snapshot_fixture(snapshot);
	server.send_all(std::string_view(frame).substr(0, 19));
	std::thread send_remainder([&server, &frame]() {
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
		server.send_all(std::string_view(frame).substr(19));
	});

	bool callback_received_exact_snapshot = false;
	bool acknowledgement_preceded_commit = false;
	const LifecycleRenderCommit committed{7, protocol::PresentationResult::Committed};
	const auto result = client->receive_render_and_acknowledge_until(
		std::chrono::steady_clock::now() + std::chrono::seconds(1),
		[&](const protocol::Snapshot &received) {
			callback_received_exact_snapshot =
				received.snapshot_id == snapshot.snapshot_id &&
				received.event_sequence == snapshot.event_sequence &&
				received.display_mode == snapshot.display_mode;
			acknowledgement_preceded_commit = server.payload_available();
			return PanelUiResult<LifecycleRenderCommit>::success(committed);
		});
	send_remainder.join();
	TEST_REQUIRE(result);
	TEST_REQUIRE(callback_received_exact_snapshot);
	TEST_REQUIRE(!acknowledgement_preceded_commit);
	TEST_REQUIRE(result.value().render_generation == committed.render_generation);
	TEST_REQUIRE(server.read_line() == expected_acknowledgement(snapshot, committed));
	TEST_REQUIRE(client->connected());
	return true;
}

bool explicit_render_failure_sends_exact_failed_ack_and_generation_must_increase()
{
	TemporaryLifecycleServer server;
	auto client = connect_client(server);
	const auto first = snapshot_fixture(std::string(kSessionA), std::string(kChallengeA), 1);
	server.send_all(encode_snapshot_fixture(first));
	const LifecycleRenderCommit failed{3, protocol::PresentationResult::Failed};
	const auto first_result = client->receive_render_and_acknowledge_until(
		std::chrono::steady_clock::now() + std::chrono::seconds(1),
		[&](const protocol::Snapshot &) {
			return PanelUiResult<LifecycleRenderCommit>::success(failed);
		});
	TEST_REQUIRE(first_result);
	TEST_REQUIRE(first_result.value().presentation_result ==
		     protocol::PresentationResult::Failed);
	TEST_REQUIRE(server.read_line() == expected_acknowledgement(first, failed));

	const auto second = snapshot_fixture(std::string(kSessionA), std::string(kChallengeA), 2);
	server.send_all(encode_snapshot_fixture(second));
	const auto second_result = client->receive_render_and_acknowledge_until(
		std::chrono::steady_clock::now() + std::chrono::seconds(1),
		[&](const protocol::Snapshot &) {
			return PanelUiResult<LifecycleRenderCommit>::success(
				LifecycleRenderCommit{3, protocol::PresentationResult::Committed});
		});
	TEST_REQUIRE(!second_result);
	TEST_REQUIRE(second_result.error().code() ==
		     PanelUiErrorCode::LifecycleRenderGenerationNonmonotonic);
	TEST_REQUIRE(!client->connected());
	TEST_REQUIRE(server.client_closed());
	return true;
}

bool coalesced_snapshots_and_oversized_frame_fail_dark_without_rendering()
{
	{
		TemporaryLifecycleServer server;
		auto client = connect_client(server);
		const auto first =
			snapshot_fixture(std::string(kSessionA), std::string(kChallengeA), 10);
		const auto second =
			snapshot_fixture(std::string(kSessionA), std::string(kChallengeA), 11);
		server.send_all(encode_snapshot_fixture(first) + encode_snapshot_fixture(second));
		bool rendered = false;
		const auto result = client->receive_render_and_acknowledge_until(
			std::chrono::steady_clock::now() + std::chrono::seconds(1),
			[&](const protocol::Snapshot &) {
				rendered = true;
				return PanelUiResult<LifecycleRenderCommit>::success(
					LifecycleRenderCommit{1,
							      protocol::PresentationResult::Committed});
			});
		TEST_REQUIRE(!result);
		TEST_REQUIRE(result.error().code() == PanelUiErrorCode::LifecycleProtocolInvalid);
		TEST_REQUIRE(!rendered);
		TEST_REQUIRE(server.client_closed());
	}
	{
		TemporaryLifecycleServer server;
		auto client = connect_client(server);
		std::string oversized(protocol::kMaximumFrameBytes, 'x');
		oversized.push_back('\n');
		server.send_all(oversized);
		const auto result = client->receive_until(
			std::chrono::steady_clock::now() + std::chrono::seconds(1));
		TEST_REQUIRE(!result);
		TEST_REQUIRE(result.error().code() == PanelUiErrorCode::LifecycleProtocolInvalid);
		TEST_REQUIRE(result.error().context().at("protocol_error_code") ==
			     "PANEL_MODE_PROTOCOL.FRAME_TOO_LARGE");
		TEST_REQUIRE(server.client_closed());
	}
	return true;
}

bool release_and_peer_uid_mismatches_close_without_acknowledgement()
{
	{
		TemporaryLifecycleServer server;
		auto client = connect_client(server);
		const auto mismatched = snapshot_fixture(
			std::string(kSessionA), std::string(kChallengeA), 1,
			protocol::DisplayMode::DayActive, "different-release");
		server.send_all(encode_snapshot_fixture(mismatched));
		bool rendered = false;
		const auto result = client->receive_render_and_acknowledge_until(
			std::chrono::steady_clock::now() + std::chrono::seconds(1),
			[&](const protocol::Snapshot &) {
				rendered = true;
				return PanelUiResult<LifecycleRenderCommit>::success(
					LifecycleRenderCommit{1,
							      protocol::PresentationResult::Committed});
			});
		TEST_REQUIRE(!result);
		TEST_REQUIRE(result.error().code() == PanelUiErrorCode::LifecycleReleaseMismatch);
		TEST_REQUIRE(result.error().message().find("different-release") == std::string::npos);
		TEST_REQUIRE(!rendered);
		TEST_REQUIRE(server.client_closed());
	}
	{
		TemporaryLifecycleServer server;
		auto client = connect_client(server);
		const auto first =
			snapshot_fixture(std::string(kSessionA), std::string(kChallengeA), 1);
		server.send_all(encode_snapshot_fixture(first));
		const LifecycleRenderCommit first_commit{
			1, protocol::PresentationResult::Committed};
		TEST_REQUIRE(client->receive_render_and_acknowledge_until(
			std::chrono::steady_clock::now() + std::chrono::seconds(1),
			[&](const protocol::Snapshot &) {
				return PanelUiResult<LifecycleRenderCommit>::success(first_commit);
			}));
		TEST_REQUIRE(server.read_line() == expected_acknowledgement(first, first_commit));

		const auto later_mismatch = snapshot_fixture(
			std::string(kSessionA), std::string(kChallengeA), 2,
			protocol::DisplayMode::DayActive, "different-release");
		server.send_all(encode_snapshot_fixture(later_mismatch));
		const auto result = client->receive_until(
			std::chrono::steady_clock::now() + std::chrono::seconds(1));
		TEST_REQUIRE(!result);
		TEST_REQUIRE(result.error().code() ==
			     PanelUiErrorCode::LifecycleReleaseMismatch);
		TEST_REQUIRE(server.client_closed());
	}
	{
		TemporaryLifecycleServer server;
		const std::uint32_t actual_uid = static_cast<std::uint32_t>(::geteuid());
		const std::uint32_t wrong_uid =
			actual_uid == std::numeric_limits<std::uint32_t>::max() ? actual_uid - 1U
									 : actual_uid + 1U;
		const auto client_result =
			UnixSocketLifecycleClient::connect(server.socket_path(), wrong_uid);
		TEST_REQUIRE(!client_result);
		TEST_REQUIRE(client_result.error().code() ==
			     PanelUiErrorCode::LifecycleSocketPeerMismatch);
		server.accept_client();
		TEST_REQUIRE(server.client_closed());
	}
	return true;
}

bool receive_timeout_truncated_eof_and_digest_mismatch_fail_dark()
{
	{
		TemporaryLifecycleServer server;
		auto client = connect_client(server);
		const auto result = client->receive_until(
			std::chrono::steady_clock::now() + std::chrono::milliseconds(10));
		TEST_REQUIRE(!result);
		TEST_REQUIRE(result.error().code() ==
			     PanelUiErrorCode::LifecycleSocketReceiveTimedOut);
		TEST_REQUIRE(server.client_closed());
	}
	{
		TemporaryLifecycleServer server;
		auto client = connect_client(server);
		server.send_all("{\"protocol_version\":2");
		server.close_client();
		const auto result = client->receive_until(
			std::chrono::steady_clock::now() + std::chrono::seconds(1));
		TEST_REQUIRE(!result);
		TEST_REQUIRE(result.error().code() == PanelUiErrorCode::LifecycleProtocolInvalid);
		TEST_REQUIRE(result.error().context().at("protocol_error_code") ==
			     "PANEL_MODE_PROTOCOL.FRAME_ENCODING_INVALID");
	}
	{
		TemporaryLifecycleServer server;
		auto client = connect_client(server);
		auto snapshot =
			snapshot_fixture(std::string(kSessionA), std::string(kChallengeA), 1);
		std::string frame = encode_snapshot_fixture(snapshot);
		const std::string original = "\"effective_brightness_percent\":80";
		const auto position = frame.find(original);
		TEST_REQUIRE(position != std::string::npos);
		frame.replace(position, original.size(), "\"effective_brightness_percent\":79");
		server.send_all(frame);
		const auto result = client->receive_until(
			std::chrono::steady_clock::now() + std::chrono::seconds(1));
		TEST_REQUIRE(!result);
		TEST_REQUIRE(result.error().code() == PanelUiErrorCode::LifecycleProtocolInvalid);
		TEST_REQUIRE(result.error().context().at("protocol_error_code") ==
			     "PANEL_MODE_PROTOCOL.SNAPSHOT_ID_MISMATCH");
		TEST_REQUIRE(result.error().message().find(snapshot.snapshot_id) ==
			     std::string::npos);
		TEST_REQUIRE(server.client_closed());
	}
	return true;
}

bool sequence_session_and_challenge_are_connection_local_and_restart_resets_state()
{
	{
		TemporaryLifecycleServer server;
		auto client = connect_client(server);
		const auto first =
			snapshot_fixture(std::string(kSessionA), std::string(kChallengeA), 90);
		server.send_all(encode_snapshot_fixture(first));
		const LifecycleRenderCommit committed{12,
						      protocol::PresentationResult::Committed};
		TEST_REQUIRE(client->receive_render_and_acknowledge_until(
			std::chrono::steady_clock::now() + std::chrono::seconds(1),
			[&](const protocol::Snapshot &) {
				return PanelUiResult<LifecycleRenderCommit>::success(committed);
			}));
		TEST_REQUIRE(server.read_line() == expected_acknowledgement(first, committed));

		const auto duplicate =
			snapshot_fixture(std::string(kSessionA), std::string(kChallengeA), 90);
		server.send_all(encode_snapshot_fixture(duplicate));
		const auto duplicate_result = client->receive_until(
			std::chrono::steady_clock::now() + std::chrono::seconds(1));
		TEST_REQUIRE(!duplicate_result);
		TEST_REQUIRE(duplicate_result.error().code() ==
			     PanelUiErrorCode::LifecycleSequenceNonmonotonic);
		TEST_REQUIRE(server.client_closed());
	}
	{
		TemporaryLifecycleServer server;
		auto client = connect_client(server);
		const auto first =
			snapshot_fixture(std::string(kSessionA), std::string(kChallengeA), 1);
		server.send_all(encode_snapshot_fixture(first));
		const LifecycleRenderCommit first_commit{1,
							protocol::PresentationResult::Committed};
		TEST_REQUIRE(client->receive_render_and_acknowledge_until(
			std::chrono::steady_clock::now() + std::chrono::seconds(1),
			[&](const protocol::Snapshot &) {
				return PanelUiResult<LifecycleRenderCommit>::success(first_commit);
			}));
		TEST_REQUIRE(server.read_line() == expected_acknowledgement(first, first_commit));

		const auto changed_session =
			snapshot_fixture(std::string(kSessionB), std::string(kChallengeA), 2);
		server.send_all(encode_snapshot_fixture(changed_session));
		const auto changed_result = client->receive_until(
			std::chrono::steady_clock::now() + std::chrono::seconds(1));
		TEST_REQUIRE(!changed_result);
		TEST_REQUIRE(changed_result.error().code() ==
			     PanelUiErrorCode::LifecycleSessionMismatch);
		TEST_REQUIRE(server.client_closed());
	}
	{
		TemporaryLifecycleServer server;
		auto client = connect_client(server);
		const auto first =
			snapshot_fixture(std::string(kSessionA), std::string(kChallengeA), 1);
		server.send_all(encode_snapshot_fixture(first));
		const LifecycleRenderCommit first_commit{1,
							protocol::PresentationResult::Committed};
		TEST_REQUIRE(client->receive_render_and_acknowledge_until(
			std::chrono::steady_clock::now() + std::chrono::seconds(1),
			[&](const protocol::Snapshot &) {
				return PanelUiResult<LifecycleRenderCommit>::success(first_commit);
			}));
		TEST_REQUIRE(server.read_line() == expected_acknowledgement(first, first_commit));

		const auto changed_challenge =
			snapshot_fixture(std::string(kSessionA), std::string(kChallengeB), 2);
		server.send_all(encode_snapshot_fixture(changed_challenge));
		const auto changed_result = client->receive_until(
			std::chrono::steady_clock::now() + std::chrono::seconds(1));
		TEST_REQUIRE(!changed_result);
		TEST_REQUIRE(changed_result.error().code() ==
			     PanelUiErrorCode::LifecycleChallengeMismatch);
		TEST_REQUIRE(server.client_closed());
	}
	{
		TemporaryLifecycleServer restarted_server;
		auto restarted_client = connect_client(restarted_server);
		const auto restarted =
			snapshot_fixture(std::string(kSessionB), std::string(kChallengeB), 1);
		restarted_server.send_all(encode_snapshot_fixture(restarted));
		const LifecycleRenderCommit restarted_commit{
			1, protocol::PresentationResult::Committed};
		const auto restarted_result =
			restarted_client->receive_render_and_acknowledge_until(
				std::chrono::steady_clock::now() + std::chrono::seconds(1),
				[&](const protocol::Snapshot &) {
					return PanelUiResult<LifecycleRenderCommit>::success(
						restarted_commit);
				});
		TEST_REQUIRE(restarted_result);
		TEST_REQUIRE(restarted_result.value().render_generation == 1);
		TEST_REQUIRE(restarted_server.read_line() ==
			     expected_acknowledgement(restarted, restarted_commit));
	}
	return true;
}

bool invalid_socket_configuration_fails_before_connecting()
{
	const auto result = UnixSocketLifecycleClient::connect(
		"relative/mode.sock", static_cast<std::uint32_t>(::geteuid()));
	TEST_REQUIRE(!result);
	TEST_REQUIRE(result.error().code() ==
		     PanelUiErrorCode::LifecycleSocketConfigurationInvalid);
	return true;
}

using TestFunction = bool (*)();

} // namespace
} // namespace house_panel::panel_ui

int main()
{
	using namespace house_panel::panel_ui;
	const std::array<std::pair<std::string_view, TestFunction>, 7> tests{{
		{"fragmented exact acknowledgement",
		 fragmented_snapshot_is_committed_only_after_callback_and_ack_is_exact},
		{"explicit render failure",
		 explicit_render_failure_sends_exact_failed_ack_and_generation_must_increase},
		{"coalescing and bounds",
		 coalesced_snapshots_and_oversized_frame_fail_dark_without_rendering},
		{"release and peer UID mismatch",
		 release_and_peer_uid_mismatches_close_without_acknowledgement},
		{"timeout and malformed input",
		 receive_timeout_truncated_eof_and_digest_mismatch_fail_dark},
		{"restart and connection bindings",
		 sequence_session_and_challenge_are_connection_local_and_restart_resets_state},
		{"invalid configuration", invalid_socket_configuration_fails_before_connecting},
	}};
	for (const auto &[name, test] : tests) {
		if (!test()) {
			std::cerr << "FAILED: " << name << '\n';
			return 1;
		}
	}
	std::cout << "unix_socket_lifecycle_client_test: " << tests.size() << " passed\n";
	return 0;
}
