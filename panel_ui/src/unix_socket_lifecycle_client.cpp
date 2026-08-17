#include "panel_ui/unix_socket_lifecycle_client.hpp"

#include "bhp/application_identity.h"
#include "bhp/panel_mode_protocol/codec.h"
#include "bhp/panel_mode_protocol/error.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <map>
#include <poll.h>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#include <utility>

namespace house_panel::panel_ui {
namespace {

namespace protocol = bhp::panel_mode_protocol;

bool is_explicit_socket_path(const std::string &path) noexcept
{
	return !path.empty() && path.front() == '/' &&
	       path.find("REPLACE_ME") == std::string::npos &&
	       path.find('\0') == std::string::npos && path.find('\n') == std::string::npos &&
	       path.find('\r') == std::string::npos;
}

PanelUiError lifecycle_error(const PanelUiErrorCode code, std::string message,
			     const std::string &operation)
{
	return PanelUiError(code, std::move(message), {{"operation", operation}});
}

PanelUiError socket_error(const PanelUiErrorCode code, std::string message,
			  const std::string &operation, const std::string &socket_path,
			  const int error_number)
{
	return PanelUiError(code, std::move(message),
			    {{"errno", std::to_string(error_number)},
			     {"operation", operation},
			     {"socket_path", socket_path}});
}

PanelUiError protocol_error(const protocol::Error &error, const std::string &operation)
{
	std::map<std::string, std::string> context{
		{"operation", operation},
		{"protocol_error_code", std::string(protocol::to_string(error.code))},
	};
	for (const std::string_view allowed_key :
	     {std::string_view("field"), std::string_view("maximum_bytes")}) {
		const auto value = error.context.find(std::string(allowed_key));
		if (value != error.context.end()) {
			context.emplace(value->first, value->second);
		}
	}
	return PanelUiError(PanelUiErrorCode::LifecycleProtocolInvalid,
			    "The supervisor sent an invalid lifecycle protocol frame",
			    std::move(context));
}

bool configure_socket_descriptor(const int socket_descriptor) noexcept
{
	const int descriptor_flags = ::fcntl(socket_descriptor, F_GETFD, 0);
	if (descriptor_flags < 0 ||
	    ::fcntl(socket_descriptor, F_SETFD, descriptor_flags | FD_CLOEXEC) < 0) {
		return false;
	}
	const int status_flags = ::fcntl(socket_descriptor, F_GETFL, 0);
	if (status_flags < 0 ||
	    ::fcntl(socket_descriptor, F_SETFL, status_flags | O_NONBLOCK) < 0) {
		return false;
	}
#if defined(SO_NOSIGPIPE)
	const int enabled = 1;
	if (::setsockopt(socket_descriptor, SOL_SOCKET, SO_NOSIGPIPE, &enabled,
			 static_cast<socklen_t>(sizeof(enabled))) != 0) {
		return false;
	}
#endif
	return true;
}

PanelUiResult<std::uint32_t> peer_uid(const int socket_descriptor)
{
#if defined(__linux__)
	struct ucred credentials {};
	socklen_t size = static_cast<socklen_t>(sizeof(credentials));
	if (::getsockopt(socket_descriptor, SOL_SOCKET, SO_PEERCRED, &credentials, &size) != 0 ||
	    size != sizeof(credentials)) {
		return PanelUiResult<std::uint32_t>::failure(lifecycle_error(
			PanelUiErrorCode::LifecycleSocketPeerIdentityFailed,
			"Failed to read exact kernel peer credentials for the lifecycle socket",
			"unix_socket_lifecycle_client.peer_uid"));
	}
	return PanelUiResult<std::uint32_t>::success(
		static_cast<std::uint32_t>(credentials.uid));
#else
	uid_t effective_uid = 0;
	gid_t effective_gid = 0;
	if (::getpeereid(socket_descriptor, &effective_uid, &effective_gid) != 0) {
		return PanelUiResult<std::uint32_t>::failure(lifecycle_error(
			PanelUiErrorCode::LifecycleSocketPeerIdentityFailed,
			"Failed to read exact kernel peer credentials for the lifecycle socket",
			"unix_socket_lifecycle_client.peer_uid"));
	}
	return PanelUiResult<std::uint32_t>::success(
		static_cast<std::uint32_t>(effective_uid));
#endif
}

int poll_timeout_milliseconds(const std::chrono::steady_clock::time_point deadline)
{
	const auto now = std::chrono::steady_clock::now();
	if (now >= deadline) {
		return 0;
	}
	const auto remaining =
		std::chrono::ceil<std::chrono::milliseconds>(deadline - now).count();
	return static_cast<int>(std::min<long long>(
		remaining, static_cast<long long>(std::numeric_limits<int>::max())));
}

bool presentation_result_is_valid(const protocol::PresentationResult result) noexcept
{
	return result == protocol::PresentationResult::Committed ||
	       result == protocol::PresentationResult::Failed;
}

} // namespace

PanelUiResult<std::unique_ptr<UnixSocketLifecycleClient>>
UnixSocketLifecycleClient::connect(std::string socket_path,
				   const std::uint32_t expected_supervisor_uid)
{
	sockaddr_un socket_address{};
	if (!is_explicit_socket_path(socket_path) ||
	    socket_path.size() >= sizeof(socket_address.sun_path)) {
		return PanelUiResult<std::unique_ptr<UnixSocketLifecycleClient>>::failure(
			PanelUiError(
				PanelUiErrorCode::LifecycleSocketConfigurationInvalid,
				"Lifecycle socket configuration requires one explicit absolute path",
				{{"operation", "unix_socket_lifecycle_client.connect"},
				 {"socket_path",
				  "must_be_absolute_placeholder_free_and_fit_unix_socket"}}));
	}

	socket_address.sun_family = AF_UNIX;
	std::memcpy(socket_address.sun_path, socket_path.c_str(), socket_path.size() + 1U);
	const int socket_descriptor = ::socket(AF_UNIX, SOCK_STREAM, 0);
	if (socket_descriptor < 0) {
		return PanelUiResult<std::unique_ptr<UnixSocketLifecycleClient>>::failure(
			socket_error(PanelUiErrorCode::LifecycleSocketCreateFailed,
				     "Failed to create the lifecycle Unix stream socket",
				     "unix_socket_lifecycle_client.socket", socket_path, errno));
	}
	if (!configure_socket_descriptor(socket_descriptor)) {
		const int error_number = errno;
		(void)::close(socket_descriptor);
		return PanelUiResult<std::unique_ptr<UnixSocketLifecycleClient>>::failure(
			socket_error(PanelUiErrorCode::LifecycleSocketCreateFailed,
				     "Failed to configure the lifecycle Unix stream socket",
				     "unix_socket_lifecycle_client.configure_socket", socket_path,
				     error_number));
	}
	if (::connect(socket_descriptor, reinterpret_cast<const sockaddr *>(&socket_address),
		      static_cast<socklen_t>(sizeof(socket_address))) != 0) {
		const int error_number = errno;
		(void)::close(socket_descriptor);
		return PanelUiResult<std::unique_ptr<UnixSocketLifecycleClient>>::failure(
			socket_error(PanelUiErrorCode::LifecycleSocketConnectFailed,
				     "Failed to connect to the configured lifecycle socket",
				     "unix_socket_lifecycle_client.connect", socket_path,
				     error_number));
	}

	const auto peer_result = peer_uid(socket_descriptor);
	if (!peer_result) {
		(void)::close(socket_descriptor);
		return PanelUiResult<std::unique_ptr<UnixSocketLifecycleClient>>::failure(
			peer_result.error());
	}
	if (peer_result.value() != expected_supervisor_uid) {
		(void)::close(socket_descriptor);
		return PanelUiResult<std::unique_ptr<UnixSocketLifecycleClient>>::failure(
			lifecycle_error(PanelUiErrorCode::LifecycleSocketPeerMismatch,
					"Lifecycle socket peer UID does not match configured authority",
					"unix_socket_lifecycle_client.connect"));
	}

	return PanelUiResult<std::unique_ptr<UnixSocketLifecycleClient>>::success(
		std::unique_ptr<UnixSocketLifecycleClient>(
			new UnixSocketLifecycleClient(std::move(socket_path), socket_descriptor)));
}

UnixSocketLifecycleClient::UnixSocketLifecycleClient(std::string socket_path,
					     const int socket_descriptor)
	: socket_path_(std::move(socket_path)), socket_descriptor_(socket_descriptor)
{
}

UnixSocketLifecycleClient::~UnixSocketLifecycleClient() { close(); }

PanelUiResult<protocol::Snapshot> UnixSocketLifecycleClient::receive_validated_snapshot_until(
	const std::chrono::steady_clock::time_point deadline)
{
	if (socket_descriptor_ < 0) {
		return PanelUiResult<protocol::Snapshot>::failure(lifecycle_error(
			PanelUiErrorCode::LifecycleSocketDisconnected,
			"Cannot receive a lifecycle snapshot without a connected supervisor",
			"unix_socket_lifecycle_client.receive"));
	}
	if (deadline == std::chrono::steady_clock::time_point::max()) {
		auto failed = fail_and_close(lifecycle_error(
			PanelUiErrorCode::LifecycleSocketConfigurationInvalid,
			"Lifecycle receive requires a finite monotonic deadline",
			"unix_socket_lifecycle_client.receive"));
		return PanelUiResult<protocol::Snapshot>::failure(failed.error());
	}
	if (pending_snapshot_) {
		auto failed = fail_and_close(lifecycle_error(
			PanelUiErrorCode::LifecycleProtocolInvalid,
			"A lifecycle snapshot is already pending an explicit presentation result",
			"unix_socket_lifecycle_client.receive"));
		return PanelUiResult<protocol::Snapshot>::failure(failed.error());
	}

	while (std::chrono::steady_clock::now() < deadline) {
		pollfd descriptor{socket_descriptor_, POLLIN, 0};
		const int poll_result =
			::poll(&descriptor, 1, poll_timeout_milliseconds(deadline));
		if (poll_result < 0 && errno == EINTR) {
			continue;
		}
		if (poll_result < 0) {
			auto failed = fail_and_close(socket_error(
				PanelUiErrorCode::LifecycleSocketReceiveFailed,
				"Failed while waiting for a lifecycle snapshot",
				"unix_socket_lifecycle_client.poll", socket_path_, errno));
			return PanelUiResult<protocol::Snapshot>::failure(failed.error());
		}
		if (poll_result == 0) {
			continue;
		}
		if ((descriptor.revents & POLLIN) == 0 &&
		    (descriptor.revents & (POLLHUP | POLLERR | POLLNVAL)) != 0) {
			const auto finish_result = snapshot_frames_.finish();
			if (!finish_result) {
				auto failed = fail_and_close(protocol_error(
					finish_result.error(),
					"unix_socket_lifecycle_client.receive"));
				return PanelUiResult<protocol::Snapshot>::failure(failed.error());
			}
			auto failed = fail_and_close(lifecycle_error(
				PanelUiErrorCode::LifecycleSocketDisconnected,
				"Supervisor disconnected before sending a lifecycle snapshot",
				"unix_socket_lifecycle_client.receive"));
			return PanelUiResult<protocol::Snapshot>::failure(failed.error());
		}
		if ((descriptor.revents & POLLIN) == 0) {
			continue;
		}

		std::array<char, 1024> buffer{};
		const ssize_t received =
			::recv(socket_descriptor_, buffer.data(), buffer.size(), 0);
		if (received < 0 &&
		    (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) {
			continue;
		}
		if (received < 0) {
			auto failed = fail_and_close(socket_error(
				PanelUiErrorCode::LifecycleSocketReceiveFailed,
				"Failed to receive a lifecycle snapshot",
				"unix_socket_lifecycle_client.recv", socket_path_, errno));
			return PanelUiResult<protocol::Snapshot>::failure(failed.error());
		}
		if (received == 0) {
			const auto finish_result = snapshot_frames_.finish();
			if (!finish_result) {
				auto failed = fail_and_close(protocol_error(
					finish_result.error(),
					"unix_socket_lifecycle_client.receive"));
				return PanelUiResult<protocol::Snapshot>::failure(failed.error());
			}
			auto failed = fail_and_close(lifecycle_error(
				PanelUiErrorCode::LifecycleSocketDisconnected,
				"Supervisor disconnected before sending a lifecycle snapshot",
				"unix_socket_lifecycle_client.receive"));
			return PanelUiResult<protocol::Snapshot>::failure(failed.error());
		}

		const auto frame_result = snapshot_frames_.append(
			std::string_view(buffer.data(), static_cast<std::size_t>(received)));
		if (!frame_result) {
			auto failed = fail_and_close(protocol_error(
				frame_result.error(), "unix_socket_lifecycle_client.receive"));
			return PanelUiResult<protocol::Snapshot>::failure(failed.error());
		}
		if (frame_result.value().empty()) {
			continue;
		}
		if (frame_result.value().size() != 1U || snapshot_frames_.buffered_bytes() != 0U) {
			auto failed = fail_and_close(lifecycle_error(
				PanelUiErrorCode::LifecycleProtocolInvalid,
				"Supervisor pipelined lifecycle data before presentation acknowledgement",
				"unix_socket_lifecycle_client.receive"));
			return PanelUiResult<protocol::Snapshot>::failure(failed.error());
		}

		const auto snapshot_result = protocol::decode_snapshot(frame_result.value().front());
		if (!snapshot_result) {
			auto failed = fail_and_close(protocol_error(
				snapshot_result.error(), "unix_socket_lifecycle_client.decode_snapshot"));
			return PanelUiResult<protocol::Snapshot>::failure(failed.error());
		}
		const auto digest_result = protocol::compute_snapshot_id(snapshot_result.value());
		if (!digest_result) {
			auto failed = fail_and_close(protocol_error(
				digest_result.error(), "unix_socket_lifecycle_client.verify_snapshot"));
			return PanelUiResult<protocol::Snapshot>::failure(failed.error());
		}
		if (digest_result.value() != snapshot_result.value().snapshot_id) {
			auto failed = fail_and_close(lifecycle_error(
				PanelUiErrorCode::LifecycleProtocolInvalid,
				"Typed lifecycle snapshot did not satisfy its canonical content binding",
				"unix_socket_lifecycle_client.verify_snapshot"));
			return PanelUiResult<protocol::Snapshot>::failure(failed.error());
		}
		const auto binding_result = validate_snapshot_binding(snapshot_result.value());
		if (!binding_result) {
			auto failed = fail_and_close(binding_result.error());
			return PanelUiResult<protocol::Snapshot>::failure(failed.error());
		}
		snapshot_frames_.reset();
		return PanelUiResult<protocol::Snapshot>::success(snapshot_result.value());
	}

	auto failed = fail_and_close(lifecycle_error(
		PanelUiErrorCode::LifecycleSocketReceiveTimedOut,
		"Timed out before receiving one complete lifecycle snapshot",
		"unix_socket_lifecycle_client.receive"));
	return PanelUiResult<protocol::Snapshot>::failure(failed.error());
}

PanelUiResult<bool> UnixSocketLifecycleClient::validate_snapshot_binding(
	const protocol::Snapshot &snapshot)
{
	if (snapshot.appliance_release_id != bhp::kApplianceReleaseId) {
		return PanelUiResult<bool>::failure(lifecycle_error(
			PanelUiErrorCode::LifecycleReleaseMismatch,
			"Supervisor and panel UI application releases do not match",
			"unix_socket_lifecycle_client.validate_snapshot"));
	}
	if (supervisor_session_id_.empty()) {
		supervisor_session_id_ = snapshot.supervisor_session_id;
		connection_challenge_ = snapshot.connection_challenge;
		last_event_sequence_ = snapshot.event_sequence;
		return PanelUiResult<bool>::success(true);
	}
	if (snapshot.supervisor_session_id != supervisor_session_id_) {
		return PanelUiResult<bool>::failure(lifecycle_error(
			PanelUiErrorCode::LifecycleSessionMismatch,
			"Supervisor session changed within one lifecycle connection",
			"unix_socket_lifecycle_client.validate_snapshot"));
	}
	if (snapshot.connection_challenge != connection_challenge_) {
		return PanelUiResult<bool>::failure(lifecycle_error(
			PanelUiErrorCode::LifecycleChallengeMismatch,
			"Supervisor challenge changed within one lifecycle connection",
			"unix_socket_lifecycle_client.validate_snapshot"));
	}
	if (snapshot.event_sequence <= last_event_sequence_) {
		return PanelUiResult<bool>::failure(lifecycle_error(
			PanelUiErrorCode::LifecycleSequenceNonmonotonic,
			"Lifecycle event sequence did not strictly increase within the connection",
			"unix_socket_lifecycle_client.validate_snapshot"));
	}
	last_event_sequence_ = snapshot.event_sequence;
	return PanelUiResult<bool>::success(true);
}

PanelUiResult<std::optional<protocol::Snapshot>> UnixSocketLifecycleClient::receive_until(
	const std::chrono::steady_clock::time_point deadline)
{
	const auto snapshot_result = receive_validated_snapshot_until(deadline);
	if (!snapshot_result) {
		return PanelUiResult<std::optional<protocol::Snapshot>>::failure(
			snapshot_result.error());
	}
	pending_snapshot_ = snapshot_result.value();
	return PanelUiResult<std::optional<protocol::Snapshot>>::success(pending_snapshot_);
}

PanelUiResult<LifecycleRenderCommit> UnixSocketLifecycleClient::render_and_acknowledge_pending(
	const LifecycleRenderCommitCallback &render_and_commit)
{
	if (socket_descriptor_ < 0) {
		return PanelUiResult<LifecycleRenderCommit>::failure(lifecycle_error(
			PanelUiErrorCode::LifecycleSocketDisconnected,
			"Cannot present a lifecycle snapshot without a connected supervisor",
			"unix_socket_lifecycle_client.render_and_acknowledge"));
	}
	if (!pending_snapshot_ || !render_and_commit) {
		close();
		return PanelUiResult<LifecycleRenderCommit>::failure(lifecycle_error(
			PanelUiErrorCode::LifecycleRenderFailed,
			"Lifecycle presentation requires one pending snapshot and explicit renderer",
			"unix_socket_lifecycle_client.render_and_acknowledge"));
	}

	PanelUiResult<LifecycleRenderCommit> render_result = [&]() {
		try {
			return render_and_commit(*pending_snapshot_);
		} catch (...) {
			return PanelUiResult<LifecycleRenderCommit>::failure(lifecycle_error(
				PanelUiErrorCode::LifecycleRenderFailed,
				"Lifecycle renderer did not return an explicit commit result",
				"unix_socket_lifecycle_client.render_and_acknowledge"));
		}
	}();
	if (!render_result) {
		const std::string renderer_error_code(render_result.error().code_string());
		close();
		return PanelUiResult<LifecycleRenderCommit>::failure(PanelUiError(
			PanelUiErrorCode::LifecycleRenderFailed,
			"Lifecycle renderer failed without an acknowledgeable attempted generation",
			{{"operation", "unix_socket_lifecycle_client.render_and_acknowledge"},
			 {"renderer_error_code", renderer_error_code}}));
	}

	const LifecycleRenderCommit render_commit = render_result.value();
	if (!presentation_result_is_valid(render_commit.presentation_result)) {
		close();
		return PanelUiResult<LifecycleRenderCommit>::failure(lifecycle_error(
			PanelUiErrorCode::LifecycleRenderFailed,
			"Lifecycle renderer returned an invalid presentation result",
			"unix_socket_lifecycle_client.render_and_acknowledge"));
	}
	if (render_commit.render_generation <= last_render_generation_ ||
	    render_commit.render_generation > protocol::kMaximumSafeJsonInteger) {
		close();
		return PanelUiResult<LifecycleRenderCommit>::failure(lifecycle_error(
			PanelUiErrorCode::LifecycleRenderGenerationNonmonotonic,
			"Lifecycle render generation must be a safe positive integer and "
			"strictly increase within the connection",
			"unix_socket_lifecycle_client.render_and_acknowledge"));
	}

	const auto acknowledgement_result =
		send_acknowledgement(*pending_snapshot_, render_commit);
	if (!acknowledgement_result) {
		close();
		return PanelUiResult<LifecycleRenderCommit>::failure(
			acknowledgement_result.error());
	}
	last_render_generation_ = render_commit.render_generation;
	pending_snapshot_.reset();
	return PanelUiResult<LifecycleRenderCommit>::success(render_commit);
}

PanelUiResult<LifecycleRenderCommit>
UnixSocketLifecycleClient::receive_render_and_acknowledge_until(
	const std::chrono::steady_clock::time_point deadline,
	const LifecycleRenderCommitCallback &render_and_commit)
{
	const auto snapshot_result = receive_until(deadline);
	if (!snapshot_result) {
		return PanelUiResult<LifecycleRenderCommit>::failure(snapshot_result.error());
	}
	if (!snapshot_result.value()) {
		close();
		return PanelUiResult<LifecycleRenderCommit>::failure(lifecycle_error(
			PanelUiErrorCode::LifecycleProtocolInvalid,
			"Lifecycle receive completed without a typed snapshot",
			"unix_socket_lifecycle_client.receive_render_and_acknowledge"));
	}
	return render_and_acknowledge_pending(render_and_commit);
}

PanelUiResult<bool> UnixSocketLifecycleClient::send_acknowledgement(
	const protocol::Snapshot &snapshot, const LifecycleRenderCommit &render_commit)
{
	const protocol::PresentationAcknowledgement acknowledgement{
		std::string(bhp::kApplianceReleaseId),
		snapshot.supervisor_session_id,
		snapshot.connection_challenge,
		snapshot.event_sequence,
		snapshot.snapshot_id,
		snapshot.display_mode,
		render_commit.render_generation,
		render_commit.presentation_result,
	};
	const auto frame_result = protocol::encode_presentation_acknowledgement(acknowledgement);
	if (!frame_result) {
		return PanelUiResult<bool>::failure(PanelUiError(
			PanelUiErrorCode::LifecycleAcknowledgementEncodeFailed,
			"Failed to encode the exact lifecycle presentation acknowledgement",
			{{"operation", "unix_socket_lifecycle_client.encode_acknowledgement"},
			 {"protocol_error_code",
			  std::string(protocol::to_string(frame_result.error().code))}}));
	}

	const std::string &frame = frame_result.value();
	std::size_t sent = 0;
	while (sent < frame.size()) {
#if defined(MSG_NOSIGNAL)
		constexpr int flags = MSG_NOSIGNAL;
#else
		constexpr int flags = 0;
#endif
		const ssize_t result = ::send(socket_descriptor_, frame.data() + sent,
					      frame.size() - sent, flags);
		if (result < 0 && errno == EINTR) {
			continue;
		}
		if (result <= 0) {
			return PanelUiResult<bool>::failure(socket_error(
				PanelUiErrorCode::LifecycleSocketSendFailed,
				"Failed to send the exact lifecycle presentation acknowledgement",
				"unix_socket_lifecycle_client.send_acknowledgement", socket_path_,
				result == 0 ? EPIPE : errno));
		}
		sent += static_cast<std::size_t>(result);
	}
	return PanelUiResult<bool>::success(true);
}

PanelUiResult<bool> UnixSocketLifecycleClient::fail_and_close(PanelUiError error)
{
	close();
	return PanelUiResult<bool>::failure(std::move(error));
}

void UnixSocketLifecycleClient::close() noexcept
{
	if (socket_descriptor_ >= 0) {
		(void)::close(socket_descriptor_);
		socket_descriptor_ = -1;
	}
	snapshot_frames_.reset();
	supervisor_session_id_.clear();
	connection_challenge_.clear();
	last_event_sequence_ = 0;
	last_render_generation_ = 0;
	pending_snapshot_.reset();
}

bool UnixSocketLifecycleClient::connected() const noexcept { return socket_descriptor_ >= 0; }

const std::string &UnixSocketLifecycleClient::socket_path() const noexcept
{
	return socket_path_;
}

std::uint64_t UnixSocketLifecycleClient::last_render_generation() const noexcept
{
	return last_render_generation_;
}

} // namespace house_panel::panel_ui
