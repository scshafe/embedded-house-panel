#include "panel_supervisor/unix_socket_ui_mode_notifier.hpp"

#include "panel_supervisor/error_code.hpp"

#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <iomanip>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <utility>

namespace house_panel::supervisor {
namespace {

constexpr int kUiSocketBacklog = 1;
constexpr unsigned int kUiSocketPermissions = 0660;

struct SocketPathIdentity {
	std::uint64_t device;
	std::uint64_t inode;
	std::uint64_t owner;
};

bool is_explicit_socket_path(const std::string &path) noexcept
{
	return !path.empty() && path.front() == '/' &&
	       path.find("REPLACE_ME") == std::string::npos &&
	       path.find('\n') == std::string::npos && path.find('\r') == std::string::npos;
}

SupervisorError socket_error(const ErrorCode code, const std::string &message,
			     const std::string &path, const char *operation,
			     const int error_number, const bool retryable = false)
{
	return SupervisorError(code, message,
			       {{"errno", std::to_string(error_number)},
				{"operation", operation},
				{"socket_path", path}},
			       retryable);
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

bool socket_identity_matches(const std::string &path,
			     const SocketPathIdentity &expected_identity) noexcept
{
	struct stat socket_status {};
	return ::lstat(path.c_str(), &socket_status) == 0 && S_ISSOCK(socket_status.st_mode) &&
	       static_cast<std::uint64_t>(socket_status.st_uid) == expected_identity.owner &&
	       static_cast<std::uint64_t>(socket_status.st_dev) == expected_identity.device &&
	       static_cast<std::uint64_t>(socket_status.st_ino) == expected_identity.inode;
}

void unlink_matching_socket_noexcept(const std::string &path,
				     const SocketPathIdentity &expected_identity) noexcept
{
	if (socket_identity_matches(path, expected_identity)) {
		(void)::unlink(path.c_str());
	}
}

Result<SocketPathIdentity> inspect_created_socket(const std::string &path)
{
	struct stat socket_status {};
	if (::lstat(path.c_str(), &socket_status) != 0) {
		return Result<SocketPathIdentity>::failure(socket_error(
			ErrorCode::UiSocketStalePathInspectionFailed,
			"Failed to inspect the newly bound panel mode socket path", path,
			"unix_socket_ui_mode_notifier.inspect_created_socket", errno));
	}
	if (!S_ISSOCK(socket_status.st_mode) || socket_status.st_uid != ::geteuid()) {
		return Result<SocketPathIdentity>::failure(SupervisorError(
			ErrorCode::UiSocketStalePathInspectionFailed,
			"Failed to trust the newly bound panel mode path type or owner",
			{{"expected_owner", std::to_string(::geteuid())},
			 {"operation", "unix_socket_ui_mode_notifier.inspect_created_socket"},
			 {"socket_path", path}}));
	}
	return Result<SocketPathIdentity>::success(
		SocketPathIdentity{static_cast<std::uint64_t>(socket_status.st_dev),
				   static_cast<std::uint64_t>(socket_status.st_ino),
				   static_cast<std::uint64_t>(socket_status.st_uid)});
}

Result<bool> remove_stale_socket_if_safe(const std::string &path,
					 const sockaddr_un &socket_address)
{
	struct stat initial_status {};
	if (::lstat(path.c_str(), &initial_status) != 0) {
		if (errno == ENOENT) {
			return Result<bool>::success(true);
		}
		return Result<bool>::failure(socket_error(
			ErrorCode::UiSocketStalePathInspectionFailed,
			"Failed to inspect an existing panel mode socket path", path,
			"unix_socket_ui_mode_notifier.inspect_stale_socket", errno));
	}
	if (!S_ISSOCK(initial_status.st_mode) || initial_status.st_uid != ::geteuid()) {
		return Result<bool>::success(false);
	}
	const SocketPathIdentity initial_identity{
		static_cast<std::uint64_t>(initial_status.st_dev),
		static_cast<std::uint64_t>(initial_status.st_ino),
		static_cast<std::uint64_t>(initial_status.st_uid),
	};

	const int probe_socket = ::socket(AF_UNIX, SOCK_STREAM, 0);
	if (probe_socket < 0) {
		return Result<bool>::failure(socket_error(
			ErrorCode::UiSocketStalePathInspectionFailed,
			"Failed to create a liveness probe for an existing panel mode socket", path,
			"unix_socket_ui_mode_notifier.create_liveness_probe", errno));
	}
	if (!configure_socket_descriptor(probe_socket)) {
		const int configuration_error_number = errno;
		(void)::close(probe_socket);
		return Result<bool>::failure(socket_error(
			ErrorCode::UiSocketStalePathInspectionFailed,
			"Failed to configure a liveness probe for an existing panel mode socket",
			path, "unix_socket_ui_mode_notifier.configure_liveness_probe",
			configuration_error_number));
	}
	const int connect_result = ::connect(
		probe_socket, reinterpret_cast<const sockaddr *>(&socket_address),
		static_cast<socklen_t>(sizeof(socket_address)));
	const int connect_error_number = connect_result == 0 ? 0 : errno;
	(void)::close(probe_socket);
	if (connect_result == 0 || connect_error_number == EINPROGRESS ||
	    connect_error_number == EAGAIN || connect_error_number == EALREADY ||
	    connect_error_number == EISCONN) {
		return Result<bool>::success(false);
	}
	if (connect_error_number == ENOENT) {
		return Result<bool>::success(true);
	}
	if (connect_error_number != ECONNREFUSED) {
		return Result<bool>::success(false);
	}
	if (!socket_identity_matches(path, initial_identity)) {
		return Result<bool>::success(false);
	}
	if (::unlink(path.c_str()) != 0) {
		return Result<bool>::failure(socket_error(
			ErrorCode::UiSocketStalePathRemovalFailed,
			"Failed to remove an owner-checked stale panel mode socket", path,
			"unix_socket_ui_mode_notifier.unlink_stale_socket", errno));
	}
	return Result<bool>::success(true);
}

std::string escape_protocol_json(const std::string_view value)
{
	std::ostringstream escaped;
	for (const unsigned char character : value) {
		switch (character) {
		case '"':
			escaped << "\\\"";
			break;
		case '\\':
			escaped << "\\\\";
			break;
		case '\b':
			escaped << "\\b";
			break;
		case '\f':
			escaped << "\\f";
			break;
		case '\n':
			escaped << "\\n";
			break;
		case '\r':
			escaped << "\\r";
			break;
		case '\t':
			escaped << "\\t";
			break;
		default:
			if (character < 0x20U) {
				escaped << "\\u" << std::hex << std::setw(4) << std::setfill('0')
					<< static_cast<unsigned int>(character);
			} else {
				escaped << character;
			}
		}
	}
	return escaped.str();
}

Result<std::string> utc_timestamp(const std::chrono::system_clock::time_point observed_at)
{
	const std::time_t observed_time = std::chrono::system_clock::to_time_t(observed_at);
	std::tm utc_time{};
#if defined(_WIN32)
	if (gmtime_s(&utc_time, &observed_time) != 0) {
#else
	if (gmtime_r(&observed_time, &utc_time) == nullptr) {
#endif
		return Result<std::string>::failure(SupervisorError(
			ErrorCode::UiSocketSendFailed,
			"Failed to encode panel mode snapshot because observed_at is not convertible to UTC",
			{{"failure_stage", "encode_observed_at"},
			 {"operation", "unix_socket_ui_mode_notifier.encode_notification"}}));
	}
	std::ostringstream timestamp;
	timestamp << std::put_time(&utc_time, "%Y-%m-%dT%H:%M:%SZ");
	return Result<std::string>::success(timestamp.str());
}

std::string_view display_mode(const UiMode ui_mode) noexcept
{
	switch (ui_mode) {
	case UiMode::Day:
		return "vocabulary";
	case UiMode::Night:
		return "night_red";
	case UiMode::Suspended:
		return "suspended";
	}
	return "diagnostic";
}

} // namespace

Result<std::unique_ptr<UnixSocketUiModeNotifier>>
UnixSocketUiModeNotifier::create(std::string socket_path)
{
	sockaddr_un socket_address{};
	if (!is_explicit_socket_path(socket_path) ||
	    socket_path.size() >= sizeof(socket_address.sun_path)) {
		return Result<std::unique_ptr<UnixSocketUiModeNotifier>>::failure(SupervisorError(
			ErrorCode::UiSocketConfigurationInvalid,
			"Failed to create panel mode socket because its explicit path is invalid",
			{{"maximum_path_bytes",
			  std::to_string(sizeof(socket_address.sun_path) - 1)},
			 {"operation", "unix_socket_ui_mode_notifier.create"},
			 {"socket_path",
			  "must_be_absolute_placeholder_free_and_fit_unix_socket"}}));
	}
	socket_address.sun_family = AF_UNIX;
	std::memcpy(socket_address.sun_path, socket_path.c_str(), socket_path.size() + 1);

	const int listening_socket = ::socket(AF_UNIX, SOCK_STREAM, 0);
	if (listening_socket < 0) {
		return Result<std::unique_ptr<UnixSocketUiModeNotifier>>::failure(socket_error(
			ErrorCode::UiSocketCreateFailed,
			"Failed to create the panel mode Unix stream socket", socket_path,
			"unix_socket_ui_mode_notifier.socket", errno));
	}
	if (!configure_socket_descriptor(listening_socket)) {
		const int configuration_error_number = errno;
		(void)::close(listening_socket);
		return Result<std::unique_ptr<UnixSocketUiModeNotifier>>::failure(socket_error(
			ErrorCode::UiSocketCreateFailed,
			"Failed to configure close-on-exec and nonblocking panel mode socket flags",
			socket_path, "unix_socket_ui_mode_notifier.configure_socket",
			configuration_error_number));
	}

	auto bind_socket = [&]() {
		return ::bind(listening_socket, reinterpret_cast<const sockaddr *>(&socket_address),
			      static_cast<socklen_t>(sizeof(socket_address)));
	};
	if (bind_socket() != 0) {
		const int first_bind_error_number = errno;
		if (first_bind_error_number != EADDRINUSE) {
			(void)::close(listening_socket);
			return Result<std::unique_ptr<UnixSocketUiModeNotifier>>::failure(
				socket_error(ErrorCode::UiSocketBindFailed,
					     "Failed to bind the supervisor-owned panel mode socket",
					     socket_path,
					     "unix_socket_ui_mode_notifier.bind",
					     first_bind_error_number,
					     first_bind_error_number == EINTR ||
						     first_bind_error_number == EAGAIN));
		}
		const auto stale_result =
			remove_stale_socket_if_safe(socket_path, socket_address);
		if (!stale_result) {
			(void)::close(listening_socket);
			return Result<std::unique_ptr<UnixSocketUiModeNotifier>>::failure(
				stale_result.error());
		}
		if (!stale_result.value() || bind_socket() != 0) {
			const int final_bind_error_number = stale_result.value() ? errno : EADDRINUSE;
			(void)::close(listening_socket);
			return Result<std::unique_ptr<UnixSocketUiModeNotifier>>::failure(
				socket_error(
					ErrorCode::UiSocketBindFailed,
					"Failed to bind without replacing an active, foreign, or non-socket path",
					socket_path, "unix_socket_ui_mode_notifier.bind_after_stale_check",
					final_bind_error_number));
		}
	}

	const auto identity_result = inspect_created_socket(socket_path);
	if (!identity_result) {
		(void)::close(listening_socket);
		return Result<std::unique_ptr<UnixSocketUiModeNotifier>>::failure(
			identity_result.error());
	}
	const SocketPathIdentity socket_identity = identity_result.value();
	if (::chmod(socket_path.c_str(), kUiSocketPermissions) != 0) {
		const int chmod_error_number = errno;
		(void)::close(listening_socket);
		unlink_matching_socket_noexcept(socket_path, socket_identity);
		return Result<std::unique_ptr<UnixSocketUiModeNotifier>>::failure(socket_error(
			ErrorCode::UiSocketPermissionFailed,
			"Failed to apply documented 0660 permissions to the panel mode socket",
			socket_path, "unix_socket_ui_mode_notifier.chmod", chmod_error_number));
	}
	if (::listen(listening_socket, kUiSocketBacklog) != 0) {
		const int listen_error_number = errno;
		(void)::close(listening_socket);
		unlink_matching_socket_noexcept(socket_path, socket_identity);
		return Result<std::unique_ptr<UnixSocketUiModeNotifier>>::failure(socket_error(
			ErrorCode::UiSocketListenFailed,
			"Failed to listen on the supervisor-owned panel mode socket", socket_path,
			"unix_socket_ui_mode_notifier.listen", listen_error_number));
	}

	return Result<std::unique_ptr<UnixSocketUiModeNotifier>>::success(
		std::unique_ptr<UnixSocketUiModeNotifier>(new UnixSocketUiModeNotifier(
			std::move(socket_path), listening_socket, socket_identity.device,
			socket_identity.inode)));
}

UnixSocketUiModeNotifier::UnixSocketUiModeNotifier(std::string socket_path,
						   const int listening_socket,
						   const std::uint64_t socket_device,
						   const std::uint64_t socket_inode)
	: socket_path_(std::move(socket_path)), listening_socket_(listening_socket),
	  socket_device_(socket_device), socket_inode_(socket_inode)
{
}

UnixSocketUiModeNotifier::~UnixSocketUiModeNotifier()
{
	close_client();
	(void)::close(listening_socket_);
	unlink_matching_socket_noexcept(
		socket_path_, SocketPathIdentity{socket_device_, socket_inode_, ::geteuid()});
}

Result<bool> UnixSocketUiModeNotifier::accept_pending_primary_client()
{
	bool accepted_primary_client = false;
	while (true) {
		const int accepted_socket = ::accept(listening_socket_, nullptr, nullptr);
		if (accepted_socket < 0 && errno == EINTR) {
			continue;
		}
		if (accepted_socket < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
			return Result<bool>::success(accepted_primary_client);
		}
		if (accepted_socket < 0 && errno == ECONNABORTED) {
			continue;
		}
		if (accepted_socket < 0) {
			return Result<bool>::failure(socket_error(
				ErrorCode::UiSocketAcceptFailed,
				"Failed to accept from the supervisor-owned listening socket",
				socket_path_, "unix_socket_ui_mode_notifier.accept", errno,
				errno == EMFILE || errno == ENFILE));
		}
		if (!configure_socket_descriptor(accepted_socket)) {
			(void)::close(accepted_socket);
			continue;
		}
		if (client_socket_ >= 0) {
			(void)::close(accepted_socket);
			continue;
		}
		client_socket_ = accepted_socket;
		accepted_primary_client = true;
	}
}

bool UnixSocketUiModeNotifier::send_line_to_client(const std::string &json_line) noexcept
{
	if (client_socket_ < 0) {
		return false;
	}
	std::size_t bytes_sent = 0;
	while (bytes_sent < json_line.size()) {
		int send_flags = 0;
#ifdef MSG_DONTWAIT
		send_flags |= MSG_DONTWAIT;
#endif
#ifdef MSG_NOSIGNAL
		send_flags |= MSG_NOSIGNAL;
#endif
		const ssize_t send_result =
			::send(client_socket_, json_line.data() + bytes_sent,
			       json_line.size() - bytes_sent, send_flags);
		if (send_result < 0 && errno == EINTR) {
			continue;
		}
		if (send_result <= 0) {
			close_client();
			return false;
		}
		bytes_sent += static_cast<std::size_t>(send_result);
	}
	return true;
}

void UnixSocketUiModeNotifier::close_client() noexcept
{
	if (client_socket_ >= 0) {
		(void)::close(client_socket_);
		client_socket_ = -1;
	}
}

Result<std::string> UnixSocketUiModeNotifier::encode_notification(
	const UiModeNotification &notification, const std::uint64_t sequence) const
{
	const auto timestamp_result = utc_timestamp(notification.observed_at);
	if (!timestamp_result) {
		return Result<std::string>::failure(timestamp_result.error());
	}
	std::ostringstream json;
	json << "{\"data\":{\"protocol_version\":1,\"event_sequence\":" << sequence
	     << ",\"display_mode\":\"" << display_mode(notification.ui_mode)
	     << "\",\"presence_state\":\"" << to_string(notification.presence_state)
	     << "\",\"solar_state\":\"" << to_string(notification.solar_state)
	     << "\",\"system_state\":\"" << to_string(notification.system_state)
	     << "\",\"effective_brightness\":"
	     << notification.effective_brightness_percent << ",\"observed_at\":\""
	     << escape_protocol_json(timestamp_result.value())
	     << "\"},\"error\":null,\"meta\":{\"degraded\":false,\"degraded_reason\":null,"
		"\"request_id\":\"local-"
	     << sequence << "\"}}\n";
	return Result<std::string>::success(json.str());
}

Result<PendingPresentation> UnixSocketUiModeNotifier::publish(
	const UiModeNotification &notification)
{
	if (event_sequence_ == std::numeric_limits<std::uint64_t>::max()) {
		return Result<PendingPresentation>::failure(SupervisorError(
			ErrorCode::UiSocketSequenceOverflow,
			"Failed to publish panel mode snapshot because event sequence is exhausted",
			{{"event_sequence", std::to_string(event_sequence_)},
			 {"operation", "unix_socket_ui_mode_notifier.publish"}}));
	}
	const auto accepted_result = accept_pending_primary_client();
	if (!accepted_result) {
		return Result<PendingPresentation>::failure(accepted_result.error());
	}
	if (accepted_result.value() && !current_snapshot_line_.empty()) {
		(void)send_line_to_client(current_snapshot_line_);
	}

	const std::uint64_t next_sequence = event_sequence_ + 1;
	const auto encoded_result = encode_notification(notification, next_sequence);
	if (!encoded_result) {
		return Result<PendingPresentation>::failure(encoded_result.error());
	}
	event_sequence_ = next_sequence;
	current_snapshot_line_ = encoded_result.value();
	(void)send_line_to_client(current_snapshot_line_);
	return Result<PendingPresentation>::success(PendingPresentation{event_sequence_});
}

PresentationOutcome UnixSocketUiModeNotifier::await_presentation(
	const PendingPresentation &pending_presentation,
	const std::chrono::steady_clock::time_point deadline)
{
	(void)pending_presentation;
	(void)deadline;
	return PresentationNotCommitted{
		PresentationNotCommittedReason::AcknowledgementUnavailable};
}

const std::string &UnixSocketUiModeNotifier::socket_path() const noexcept
{
	return socket_path_;
}

std::uint64_t UnixSocketUiModeNotifier::event_sequence() const noexcept
{
	return event_sequence_;
}

bool UnixSocketUiModeNotifier::has_connected_client() const noexcept
{
	return client_socket_ >= 0;
}

} // namespace house_panel::supervisor
