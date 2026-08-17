#include "panel_supervisor/unix_socket_ui_mode_notifier.hpp"

#include "panel_supervisor/error_code.hpp"

#include "bhp/panel_mode_protocol/codec.h"

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
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#include <utility>

#if defined(__linux__)
#include <sys/random.h>
#endif

namespace house_panel::supervisor {
namespace {

namespace protocol = bhp::panel_mode_protocol;

constexpr int kUiSocketBacklog = 1;
constexpr unsigned int kUiSocketPermissions = 0660;
constexpr std::size_t kIdentityBytes = 16;

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

SupervisorError protocol_error(const protocol::Error &error)
{
	auto context = error.context;
	context.emplace("protocol_error_code", std::string(protocol::to_string(error.code)));
	return SupervisorError(ErrorCode::UiSocketProtocolFailed,
			       "The panel UI sent an invalid protocol-v2 acknowledgement",
			       std::move(context));
}

void bind_expected_commitment(PendingPresentation &pending,
			      const protocol::Snapshot &snapshot,
			      const std::uint64_t minimum_render_generation_exclusive)
{
	pending.expected_commitment = PresentationCommitmentExpectation{
		snapshot.appliance_release_id,
		snapshot.supervisor_session_id,
		snapshot.connection_challenge,
		snapshot.snapshot_id,
		std::string(protocol::to_string(snapshot.display_mode)),
		minimum_render_generation_exclusive,
	};
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
	if (connect_error_number != ECONNREFUSED ||
	    !socket_identity_matches(path, initial_identity)) {
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

Result<std::string> random_identity(const char *operation)
{
	std::array<unsigned char, kIdentityBytes> bytes{};
#if defined(__linux__)
	std::size_t filled = 0;
	while (filled < bytes.size()) {
		const ssize_t result = ::getrandom(bytes.data() + filled, bytes.size() - filled, 0);
		if (result < 0 && errno == EINTR) {
			continue;
		}
		if (result <= 0) {
			return Result<std::string>::failure(SupervisorError(
				ErrorCode::UiSocketRandomFailed,
				"Failed to obtain a protocol identity from the operating-system CSPRNG",
				{{"operation", operation}}));
		}
		filled += static_cast<std::size_t>(result);
	}
#else
	::arc4random_buf(bytes.data(), bytes.size());
#endif
	bool any_nonzero = false;
	for (const unsigned char byte : bytes) {
		any_nonzero = any_nonzero || byte != 0U;
	}
	if (!any_nonzero) {
		return Result<std::string>::failure(SupervisorError(
			ErrorCode::UiSocketRandomFailed,
			"The operating-system CSPRNG returned a forbidden all-zero identity",
			{{"operation", operation}}));
	}
	constexpr char kHex[] = "0123456789abcdef";
	std::string encoded(bytes.size() * 2U, '0');
	for (std::size_t index = 0; index < bytes.size(); ++index) {
		encoded[index * 2U] = kHex[(bytes[index] >> 4U) & 0x0fU];
		encoded[index * 2U + 1U] = kHex[bytes[index] & 0x0fU];
	}
	return Result<std::string>::success(std::move(encoded));
}

Result<std::uint32_t> peer_uid(const int socket_descriptor)
{
#if defined(__linux__)
	struct ucred credentials {};
	socklen_t size = static_cast<socklen_t>(sizeof(credentials));
	if (::getsockopt(socket_descriptor, SOL_SOCKET, SO_PEERCRED, &credentials, &size) != 0 ||
	    size != sizeof(credentials) || credentials.uid < 0) {
		return Result<std::uint32_t>::failure(SupervisorError(
			ErrorCode::UiSocketPeerIdentityFailed,
			"Failed to read exact kernel peer credentials for the panel UI socket",
			{{"operation", "unix_socket_ui_mode_notifier.peer_uid"}}));
	}
	return Result<std::uint32_t>::success(static_cast<std::uint32_t>(credentials.uid));
#else
	uid_t effective_uid = 0;
	gid_t effective_gid = 0;
	if (::getpeereid(socket_descriptor, &effective_uid, &effective_gid) != 0) {
		return Result<std::uint32_t>::failure(SupervisorError(
			ErrorCode::UiSocketPeerIdentityFailed,
			"Failed to read exact kernel peer credentials for the panel UI socket",
			{{"operation", "unix_socket_ui_mode_notifier.peer_uid"}}));
	}
	return Result<std::uint32_t>::success(static_cast<std::uint32_t>(effective_uid));
#endif
}

protocol::DisplayMode display_mode(const UiMode mode) noexcept
{
	switch (mode) {
	case UiMode::Day:
		return protocol::DisplayMode::DayActive;
	case UiMode::Night:
		return protocol::DisplayMode::NightRed;
	case UiMode::Suspended:
		return protocol::DisplayMode::Suspended;
	}
	return protocol::DisplayMode::Diagnostic;
}

protocol::PresenceState presence_state(const PresenceState state) noexcept
{
	switch (state) {
	case PresenceState::Unknown:
		return protocol::PresenceState::Unknown;
	case PresenceState::Present:
		return protocol::PresenceState::Present;
	case PresenceState::GracePeriod:
		return protocol::PresenceState::GracePeriod;
	case PresenceState::Absent:
		return protocol::PresenceState::Absent;
	}
	return protocol::PresenceState::Unknown;
}

protocol::SolarState solar_state(const SolarState state) noexcept
{
	switch (state) {
	case SolarState::Unknown:
		return protocol::SolarState::Unknown;
	case SolarState::Day:
		return protocol::SolarState::Day;
	case SolarState::Night:
		return protocol::SolarState::Night;
	}
	return protocol::SolarState::Unknown;
}

protocol::SystemState system_state(const SystemState state) noexcept
{
	switch (state) {
	case SystemState::Starting:
		return protocol::SystemState::Starting;
	case SystemState::Active:
		return protocol::SystemState::Active;
	case SystemState::Suspended:
		return protocol::SystemState::Suspended;
	case SystemState::RecomputingAfterResume:
		return protocol::SystemState::RecomputingAfterResume;
	case SystemState::Faulted:
		return protocol::SystemState::Faulted;
	}
	return protocol::SystemState::Faulted;
}

} // namespace

Result<std::unique_ptr<UnixSocketUiModeNotifier>> UnixSocketUiModeNotifier::create(
	std::string socket_path, const std::uint32_t expected_panel_ui_uid,
	std::string appliance_release_id)
{
	sockaddr_un socket_address{};
	if (!is_explicit_socket_path(socket_path) ||
	    socket_path.size() >= sizeof(socket_address.sun_path) || expected_panel_ui_uid == 0U) {
		return Result<std::unique_ptr<UnixSocketUiModeNotifier>>::failure(SupervisorError(
			ErrorCode::UiSocketConfigurationInvalid,
			"Failed to create the panel mode socket because explicit configuration is invalid",
			{{"operation", "unix_socket_ui_mode_notifier.create"},
			 {"socket_path", "must_be_absolute_placeholder_free_and_fit_unix_socket"},
			 {"panel_ui_uid", "must_be_nonzero"}}));
	}
	protocol::Snapshot release_probe{std::move(appliance_release_id),
					"00000000000000000000000000000001",
					"00000000000000000000000000000001", 1, {},
					protocol::DisplayMode::Diagnostic,
					protocol::PresenceState::Unknown,
					protocol::SolarState::Unknown,
					protocol::SystemState::Starting, 0, 0};
	const auto release_validation = protocol::compute_snapshot_id(release_probe);
	if (!release_validation) {
		return Result<std::unique_ptr<UnixSocketUiModeNotifier>>::failure(
			SupervisorError(ErrorCode::UiSocketConfigurationInvalid,
					"The compiled appliance release identity is not valid for protocol v2",
					{{"operation", "unix_socket_ui_mode_notifier.create"}}));
	}
	const auto session_result =
		random_identity("unix_socket_ui_mode_notifier.create_session_identity");
	if (!session_result) {
		return Result<std::unique_ptr<UnixSocketUiModeNotifier>>::failure(
			session_result.error());
	}

	socket_address.sun_family = AF_UNIX;
	std::memcpy(socket_address.sun_path, socket_path.c_str(), socket_path.size() + 1U);
	const int listening_socket = ::socket(AF_UNIX, SOCK_STREAM, 0);
	if (listening_socket < 0) {
		return Result<std::unique_ptr<UnixSocketUiModeNotifier>>::failure(socket_error(
			ErrorCode::UiSocketCreateFailed,
			"Failed to create the panel mode Unix stream socket", socket_path,
			"unix_socket_ui_mode_notifier.socket", errno));
	}
	if (!configure_socket_descriptor(listening_socket)) {
		const int error_number = errno;
		(void)::close(listening_socket);
		return Result<std::unique_ptr<UnixSocketUiModeNotifier>>::failure(socket_error(
			ErrorCode::UiSocketCreateFailed,
			"Failed to configure close-on-exec and nonblocking panel mode socket flags",
			socket_path, "unix_socket_ui_mode_notifier.configure_socket", error_number));
	}

	auto bind_socket = [&]() {
		return ::bind(listening_socket, reinterpret_cast<const sockaddr *>(&socket_address),
			      static_cast<socklen_t>(sizeof(socket_address)));
	};
	if (bind_socket() != 0) {
		const int first_error = errno;
		if (first_error != EADDRINUSE) {
			(void)::close(listening_socket);
			return Result<std::unique_ptr<UnixSocketUiModeNotifier>>::failure(
				socket_error(ErrorCode::UiSocketBindFailed,
					     "Failed to bind the supervisor-owned panel mode socket",
					     socket_path, "unix_socket_ui_mode_notifier.bind",
					     first_error, first_error == EINTR || first_error == EAGAIN));
		}
		const auto stale_result = remove_stale_socket_if_safe(socket_path, socket_address);
		if (!stale_result) {
			(void)::close(listening_socket);
			return Result<std::unique_ptr<UnixSocketUiModeNotifier>>::failure(
				stale_result.error());
		}
		if (!stale_result.value() || bind_socket() != 0) {
			const int final_error = stale_result.value() ? errno : EADDRINUSE;
			(void)::close(listening_socket);
			return Result<std::unique_ptr<UnixSocketUiModeNotifier>>::failure(socket_error(
				ErrorCode::UiSocketBindFailed,
				"Failed to bind without replacing an active, foreign, or non-socket path",
				socket_path, "unix_socket_ui_mode_notifier.bind_after_stale_check",
				final_error));
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
		const int error_number = errno;
		(void)::close(listening_socket);
		unlink_matching_socket_noexcept(socket_path, socket_identity);
		return Result<std::unique_ptr<UnixSocketUiModeNotifier>>::failure(socket_error(
			ErrorCode::UiSocketPermissionFailed,
			"Failed to apply documented 0660 permissions to the panel mode socket",
			socket_path, "unix_socket_ui_mode_notifier.chmod", error_number));
	}
	if (::listen(listening_socket, kUiSocketBacklog) != 0) {
		const int error_number = errno;
		(void)::close(listening_socket);
		unlink_matching_socket_noexcept(socket_path, socket_identity);
		return Result<std::unique_ptr<UnixSocketUiModeNotifier>>::failure(socket_error(
			ErrorCode::UiSocketListenFailed,
			"Failed to listen on the supervisor-owned panel mode socket", socket_path,
			"unix_socket_ui_mode_notifier.listen", error_number));
	}

	return Result<std::unique_ptr<UnixSocketUiModeNotifier>>::success(
		std::unique_ptr<UnixSocketUiModeNotifier>(new UnixSocketUiModeNotifier(
			std::move(socket_path), listening_socket, socket_identity.device,
			socket_identity.inode, expected_panel_ui_uid,
			std::move(release_probe.appliance_release_id), session_result.value())));
}

UnixSocketUiModeNotifier::UnixSocketUiModeNotifier(
	std::string socket_path, const int listening_socket,
	const std::uint64_t socket_device, const std::uint64_t socket_inode,
	const std::uint32_t expected_panel_ui_uid, std::string appliance_release_id,
	std::string supervisor_session_id)
	: socket_path_(std::move(socket_path)), listening_socket_(listening_socket),
	  expected_panel_ui_uid_(expected_panel_ui_uid),
	  appliance_release_id_(std::move(appliance_release_id)),
	  supervisor_session_id_(std::move(supervisor_session_id)),
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
	bool accepted_primary = false;
	for (std::size_t attempt = 0U;
	     attempt < UnixSocketUiModeNotifier::maximum_accept_attempts_per_pass();
	     ++attempt) {
		const int accepted_socket = ::accept(listening_socket_, nullptr, nullptr);
		if (accepted_socket < 0 && errno == EINTR) {
			continue;
		}
		if (accepted_socket < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
			return Result<bool>::success(accepted_primary);
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
		const auto uid_result = peer_uid(accepted_socket);
		if (!uid_result || uid_result.value() != expected_panel_ui_uid_ ||
		    client_socket_ >= 0) {
			(void)::close(accepted_socket);
			continue;
		}
		const auto challenge_result = random_identity(
			"unix_socket_ui_mode_notifier.accept_connection_challenge");
		if (!challenge_result) {
			(void)::close(accepted_socket);
			return Result<bool>::failure(challenge_result.error());
		}
		client_socket_ = accepted_socket;
		connection_challenge_ = challenge_result.value();
		last_render_generation_ = 0;
		acknowledgement_frames_.reset();
		pending_snapshot_.reset();
		accepted_primary = true;
	}
	return Result<bool>::success(accepted_primary);
}

bool UnixSocketUiModeNotifier::send_line_to_client(const std::string &line) noexcept
{
	if (client_socket_ < 0) {
		return false;
	}
	std::size_t sent = 0;
	while (sent < line.size()) {
		int flags = 0;
#ifdef MSG_DONTWAIT
		flags |= MSG_DONTWAIT;
#endif
#ifdef MSG_NOSIGNAL
		flags |= MSG_NOSIGNAL;
#endif
		const ssize_t result =
			::send(client_socket_, line.data() + sent, line.size() - sent, flags);
		if (result < 0 && errno == EINTR) {
			continue;
		}
		if (result <= 0) {
			close_client();
			return false;
		}
		sent += static_cast<std::size_t>(result);
	}
	return true;
}

Result<protocol::Snapshot> UnixSocketUiModeNotifier::make_snapshot(
	const UiModeNotification &notification, const std::uint64_t sequence) const
{
	const auto observed_result =
		protocol::observed_at_unix_milliseconds(notification.observed_at);
	if (!observed_result) {
		return Result<protocol::Snapshot>::failure(protocol_error(observed_result.error()));
	}
	if (notification.effective_brightness_percent < 0 ||
	    notification.effective_brightness_percent > 100) {
		return Result<protocol::Snapshot>::failure(SupervisorError(
			ErrorCode::UiSocketProtocolFailed,
			"The supervisor attempted to encode an invalid lifecycle brightness",
			{{"field", "effective_brightness_percent"},
			 {"operation", "unix_socket_ui_mode_notifier.make_snapshot"}}));
	}
	protocol::Snapshot snapshot{
		appliance_release_id_, supervisor_session_id_, connection_challenge_, sequence, {},
		display_mode(notification.ui_mode), presence_state(notification.presence_state),
		solar_state(notification.solar_state), system_state(notification.system_state),
		static_cast<std::uint8_t>(notification.effective_brightness_percent),
		observed_result.value()};
	const auto digest_result = protocol::compute_snapshot_id(snapshot);
	if (!digest_result) {
		return Result<protocol::Snapshot>::failure(protocol_error(digest_result.error()));
	}
	snapshot.snapshot_id = digest_result.value();
	return Result<protocol::Snapshot>::success(std::move(snapshot));
}

Result<bool> UnixSocketUiModeNotifier::send_latest_snapshot_to_client()
{
	if (client_socket_ < 0 || !latest_notification_) {
		return Result<bool>::success(false);
	}
	const auto snapshot_result = make_snapshot(*latest_notification_, event_sequence_);
	if (!snapshot_result) {
		return Result<bool>::failure(snapshot_result.error());
	}
	const auto frame_result = protocol::encode_snapshot(snapshot_result.value());
	if (!frame_result) {
		return Result<bool>::failure(protocol_error(frame_result.error()));
	}
	pending_snapshot_ = snapshot_result.value();
	if (!send_line_to_client(frame_result.value())) {
		pending_snapshot_.reset();
		return Result<bool>::success(false);
	}
	return Result<bool>::success(true);
}

void UnixSocketUiModeNotifier::invalidate_pending() noexcept
{
	pending_snapshot_.reset();
	acknowledgement_frames_.reset();
}

void UnixSocketUiModeNotifier::close_client() noexcept
{
	if (client_socket_ >= 0) {
		(void)::close(client_socket_);
		client_socket_ = -1;
	}
	connection_challenge_.clear();
	last_render_generation_ = 0;
	invalidate_pending();
}

Result<PendingPresentation> UnixSocketUiModeNotifier::publish(
	const UiModeNotification &notification)
{
	if (event_sequence_ >= protocol::kMaximumSafeJsonInteger) {
		return Result<PendingPresentation>::failure(SupervisorError(
			ErrorCode::UiSocketSequenceOverflow,
			"Failed to publish a lifecycle snapshot because the event sequence is exhausted",
			{{"bound", std::to_string(protocol::kMaximumSafeJsonInteger)},
			 {"operation", "unix_socket_ui_mode_notifier.publish"}}));
	}
	invalidate_pending();
	latest_notification_ = notification;
	++event_sequence_;
	const auto accepted_result = accept_pending_primary_client();
	if (!accepted_result) {
		return Result<PendingPresentation>::failure(accepted_result.error());
	}
	if (client_socket_ >= 0) {
		const auto send_result = send_latest_snapshot_to_client();
		if (!send_result) {
			return Result<PendingPresentation>::failure(send_result.error());
		}
	}
	PendingPresentation pending{event_sequence_, std::nullopt};
	if (pending_snapshot_.has_value()) {
		bind_expected_commitment(pending, *pending_snapshot_,
					 last_render_generation_);
	}
	return Result<PendingPresentation>::success(std::move(pending));
}

PresentationOutcome UnixSocketUiModeNotifier::await_presentation(
	const PendingPresentation &pending_presentation,
	const std::chrono::steady_clock::time_point deadline)
{
	auto fail_protocol = [this](SupervisorError error) -> PresentationOutcome {
		close_client();
		return PresentationFailed{PresentationFailureReason::ExplicitFailure,
					  std::move(error)};
	};
	if (!latest_notification_ || pending_presentation.publication_sequence != event_sequence_) {
		return PresentationNotCommitted{PresentationNotCommittedReason::Stale};
	}

	while (std::chrono::steady_clock::now() < deadline) {
		if (client_socket_ < 0) {
			const auto accepted_result = accept_pending_primary_client();
			if (!accepted_result) {
				return PresentationFailed{PresentationFailureReason::ExplicitFailure,
							  accepted_result.error()};
			}
			if (accepted_result.value()) {
				return PresentationNotCommitted{
					PresentationNotCommittedReason::AcknowledgementUnavailable};
			}
		}

		const auto remaining = deadline - std::chrono::steady_clock::now();
		const auto remaining_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
			remaining + std::chrono::milliseconds(1));
		pollfd descriptors[2]{
			{listening_socket_, POLLIN, 0},
			{client_socket_, static_cast<short>(client_socket_ >= 0 ? POLLIN : 0), 0}};
		const int result = ::poll(descriptors, 2,
					  static_cast<int>(std::clamp<long long>(remaining_ms.count(), 1,
									 60'000)));
		if (result < 0 && errno == EINTR) {
			continue;
		}
		if (result < 0) {
			return fail_protocol(socket_error(
				ErrorCode::UiSocketReceiveFailed,
				"Failed while waiting for a panel UI presentation acknowledgement",
				socket_path_, "unix_socket_ui_mode_notifier.poll", errno, true));
		}
		if (result == 0) {
			continue;
		}
		if ((descriptors[0].revents & POLLIN) != 0) {
			const auto accepted_result = accept_pending_primary_client();
			if (!accepted_result) {
				return PresentationFailed{PresentationFailureReason::ExplicitFailure,
							  accepted_result.error()};
			}
			if (accepted_result.value()) {
				return PresentationNotCommitted{
					PresentationNotCommittedReason::AcknowledgementUnavailable};
			}
		}
		if (client_socket_ < 0) {
			continue;
		}
		if ((descriptors[1].revents & POLLIN) == 0 &&
		    (descriptors[1].revents & (POLLHUP | POLLERR | POLLNVAL)) != 0) {
			close_client();
			return PresentationNotCommitted{PresentationNotCommittedReason::Disconnected};
		}
		if ((descriptors[1].revents & POLLIN) == 0) {
			continue;
		}

		std::array<char, 512> buffer{};
		const ssize_t received = ::recv(client_socket_, buffer.data(), buffer.size(), 0);
		if (received < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) {
			continue;
		}
		if (received == 0) {
			const auto finish_result = acknowledgement_frames_.finish();
			if (!finish_result) {
				return fail_protocol(protocol_error(finish_result.error()));
			}
			close_client();
			return PresentationNotCommitted{PresentationNotCommittedReason::Disconnected};
		}
		if (received < 0) {
			return fail_protocol(socket_error(
				ErrorCode::UiSocketReceiveFailed,
				"Failed to receive a panel UI presentation acknowledgement",
				socket_path_, "unix_socket_ui_mode_notifier.recv", errno, true));
		}
		const auto frame_result = acknowledgement_frames_.append(
			std::string_view(buffer.data(), static_cast<std::size_t>(received)));
		if (!frame_result) {
			return fail_protocol(protocol_error(frame_result.error()));
		}
		if (frame_result.value().empty()) {
			continue;
		}
		if (frame_result.value().size() != 1U ||
		    acknowledgement_frames_.buffered_bytes() != 0U) {
			return fail_protocol(SupervisorError(
				ErrorCode::UiSocketProtocolFailed,
				"The panel UI sent more than one acknowledgement for one pending snapshot",
				{{"operation", "unix_socket_ui_mode_notifier.await_presentation"}}));
		}
		const auto acknowledgement_result =
			protocol::decode_presentation_acknowledgement(frame_result.value().front());
		if (!acknowledgement_result) {
			return fail_protocol(protocol_error(acknowledgement_result.error()));
		}
		if (!pending_snapshot_) {
			return fail_protocol(SupervisorError(
				ErrorCode::UiSocketProtocolFailed,
				"The panel UI acknowledged when no snapshot was pending",
				{{"operation", "unix_socket_ui_mode_notifier.await_presentation"}}));
		}
		const auto &acknowledgement = acknowledgement_result.value();
		const auto snapshot = *pending_snapshot_;
		if (acknowledgement.appliance_release_id != appliance_release_id_ ||
		    acknowledgement.supervisor_session_id != snapshot.supervisor_session_id ||
		    acknowledgement.event_sequence != snapshot.event_sequence ||
		    acknowledgement.connection_challenge != snapshot.connection_challenge ||
		    acknowledgement.snapshot_id != snapshot.snapshot_id ||
		    acknowledgement.display_mode != snapshot.display_mode) {
			close_client();
			return PresentationNotCommitted{PresentationNotCommittedReason::Mismatched};
		}
		if (acknowledgement.render_generation <= last_render_generation_) {
			close_client();
			return PresentationNotCommitted{PresentationNotCommittedReason::Duplicate};
		}
		last_render_generation_ = acknowledgement.render_generation;
		acknowledgement_frames_.reset();
		pending_snapshot_.reset();
		if (acknowledgement.presentation_result == protocol::PresentationResult::Failed) {
			return PresentationFailed{
				PresentationFailureReason::ExplicitFailure,
				SupervisorError(ErrorCode::UiPresentationExplicitlyFailed,
						"The panel UI explicitly reported presentation failure",
						{{"operation",
						  "unix_socket_ui_mode_notifier.await_presentation"}})};
		}
		return PresentationCommitted{
			appliance_release_id_, event_sequence_, acknowledgement.render_generation,
			snapshot.supervisor_session_id, snapshot.connection_challenge,
			snapshot.snapshot_id, std::string(protocol::to_string(snapshot.display_mode))};
	}

	close_client();
	return PresentationNotCommitted{PresentationNotCommittedReason::TimedOut};
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
