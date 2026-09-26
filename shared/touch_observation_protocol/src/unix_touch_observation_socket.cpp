#include "bhp/touch_observation_protocol/unix_touch_observation_socket.h"
#include <array>
#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace bhp::touch_observation_protocol {
namespace {
Error socket_error(ErrorCode code, const char *operation) {
	return {code, "Normalized touch socket operation failed",
		{{"operation", operation}, {"errno", std::to_string(errno)}}};
}
#if defined(__linux__)
bool trusted_parent(const std::string &path) {
	if (path.empty() || path[0] != '/' || path.size() >= sizeof(sockaddr_un::sun_path)
		|| path.find('\0') != std::string::npos || path.find("//") != std::string::npos
		|| path.find("/../") != std::string::npos || path.find("/./") != std::string::npos
		|| path.back() == '/') return false;
	for (std::size_t end = 1; end != std::string::npos; end = path.find('/', end + 1)) {
		const auto parent = end == 1 ? std::string("/") : path.substr(0, end);
		struct stat info{};
		if (::lstat(parent.c_str(), &info) != 0 || !S_ISDIR(info.st_mode)
			|| info.st_uid != 0 || (info.st_mode & 0022) != 0) return false;
	}
	return true;
}
bool exact_uid(int descriptor, std::uint32_t uid) {
	struct ucred cred{}; socklen_t size = sizeof(cred);
	return ::getsockopt(descriptor, SOL_SOCKET, SO_PEERCRED, &cred, &size) == 0
		&& size == sizeof(cred) && cred.uid == uid;
}
sockaddr_un address(const std::string &path) {
	sockaddr_un value{}; value.sun_family = AF_UNIX;
	std::memcpy(value.sun_path, path.c_str(), path.size() + 1); return value;
}
bool trusted_socket(const std::string &path, std::uint32_t uid, struct stat &info) {
	return trusted_parent(path) && ::lstat(path.c_str(), &info) == 0
		&& S_ISSOCK(info.st_mode) && info.st_uid == 0 && info.st_uid == uid
		&& (info.st_mode & 0007) == 0;
}
#endif
}
UnixTouchObservationServer::UnixTouchObservationServer(std::string path, int listener,
	std::uint32_t uid, std::uint64_t device, std::uint64_t inode)
	: path_(std::move(path)), listener_(listener), uid_(uid), device_(device), inode_(inode) {}
UnixTouchObservationServer::~UnixTouchObservationServer() {
	disconnect(); ::close(listener_);
	struct stat info{};
	if (::lstat(path_.c_str(), &info) == 0 && S_ISSOCK(info.st_mode)
		&& info.st_uid == ::geteuid() && static_cast<std::uint64_t>(info.st_dev) == device_ && info.st_ino == inode_)
		::unlink(path_.c_str());
}
void UnixTouchObservationServer::disconnect() noexcept {
	if (peer_ >= 0) ::close(peer_);
	peer_ = -1;
}
Result<std::unique_ptr<UnixTouchObservationServer>> UnixTouchObservationServer::create(
	std::string path, std::uint32_t expected_ui_uid) {
#if defined(__linux__)
	if (expected_ui_uid == 0 || ::geteuid() != 0 || !trusted_parent(path))
		return Result<std::unique_ptr<UnixTouchObservationServer>>::failure(socket_error(ErrorCode::SocketUntrusted, "create.validate"));
	int listener = ::socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
	if (listener < 0) return Result<std::unique_ptr<UnixTouchObservationServer>>::failure(socket_error(ErrorCode::SocketUnavailable, "create.socket"));
	auto addr = address(path);
	// Existing paths are intentionally preserved; service runtime directories own cleanup.
	if (::bind(listener, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
		auto error = socket_error(ErrorCode::SocketUnavailable, "create.bind"); ::close(listener);
		return Result<std::unique_ptr<UnixTouchObservationServer>>::failure(error);
	}
	struct stat info{};
	if (::lstat(path.c_str(), &info) != 0 || ::chmod(path.c_str(), 0660) != 0 || ::listen(listener, 4) != 0) {
		auto error = socket_error(ErrorCode::SocketUnavailable, "create.listen");
		::close(listener); ::unlink(path.c_str());
		return Result<std::unique_ptr<UnixTouchObservationServer>>::failure(error);
	}
	return Result<std::unique_ptr<UnixTouchObservationServer>>::success(
		std::unique_ptr<UnixTouchObservationServer>(new UnixTouchObservationServer(
			std::move(path), listener, expected_ui_uid, info.st_dev, info.st_ino)));
#else
	(void)path; (void)expected_ui_uid;
	return Result<std::unique_ptr<UnixTouchObservationServer>>::failure(socket_error(ErrorCode::PlatformUnsupported, "create"));
#endif
}
Result<void> UnixTouchObservationServer::pump() {
#if defined(__linux__)
	if (peer_ >= 0) {
		char byte{}; auto read = ::recv(peer_, &byte, 1, MSG_PEEK | MSG_DONTWAIT);
		if (read >= 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
			disconnect(); return Result<void>::failure(socket_error(ErrorCode::UiDisconnected, "pump.reverse_traffic_or_disconnect"));
		}
	}
	for (int attempt = 0; attempt < 4; ++attempt) {
		int candidate = ::accept4(listener_, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
		if (candidate < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK) break;
			if (errno == EINTR) continue;
			return Result<void>::failure(socket_error(ErrorCode::SocketUnavailable, "pump.accept"));
		}
		if (peer_ >= 0 || !exact_uid(candidate, uid_)) { ::close(candidate); continue; }
		const int bytes = static_cast<int>(kMaximumTouchFrameBytes * 2);
		if (::setsockopt(candidate, SOL_SOCKET, SO_SNDBUF, &bytes, sizeof(bytes)) != 0) { ::close(candidate); continue; }
		peer_ = candidate;
	}
	return Result<void>::success();
#else
	return Result<void>::failure(socket_error(ErrorCode::PlatformUnsupported, "pump"));
#endif
}
Result<void> UnixTouchObservationServer::try_publish(const NormalizedTouchObservation &value) {
	auto frame = encode_touch_observation(value);
	if (!frame) { disconnect(); return Result<void>::failure(frame.error()); }
	auto ready = pump(); if (!ready) return ready;
	if (peer_ < 0) return Result<void>::failure(socket_error(ErrorCode::UiDisconnected, "publish"));
#if defined(__linux__)
	auto sent = ::send(peer_, frame.value().data(), frame.value().size(), MSG_DONTWAIT | MSG_NOSIGNAL);
	if (sent == static_cast<ssize_t>(frame.value().size())) return Result<void>::success();
#endif
	disconnect(); return Result<void>::failure(socket_error(ErrorCode::StreamBackpressured, "publish"));
}
UnixTouchObservationClient::~UnixTouchObservationClient() { disconnect(); }
void UnixTouchObservationClient::disconnect() noexcept {
	if (peer_ >= 0) ::close(peer_);
	peer_ = -1; buffered_.clear(); latest_sequence_ = 0;
}
Result<std::unique_ptr<UnixTouchObservationClient>> UnixTouchObservationClient::connect(
	const std::string &path, std::uint32_t expected_supervisor_uid) {
#if defined(__linux__)
	struct stat before{}, after{};
	if (!trusted_socket(path, expected_supervisor_uid, before))
		return Result<std::unique_ptr<UnixTouchObservationClient>>::failure(socket_error(ErrorCode::SocketUntrusted, "connect.path"));
	int peer = ::socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
	if (peer < 0) return Result<std::unique_ptr<UnixTouchObservationClient>>::failure(socket_error(ErrorCode::SocketUnavailable, "connect.socket"));
	auto addr = address(path);
	if (::connect(peer, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0
		|| !exact_uid(peer, expected_supervisor_uid)
		|| !trusted_socket(path, expected_supervisor_uid, after)
		|| before.st_dev != after.st_dev || before.st_ino != after.st_ino) {
		auto error = socket_error(ErrorCode::SocketUntrusted, "connect.authenticate"); ::close(peer);
		return Result<std::unique_ptr<UnixTouchObservationClient>>::failure(error);
	}
	return Result<std::unique_ptr<UnixTouchObservationClient>>::success(
		std::unique_ptr<UnixTouchObservationClient>(new UnixTouchObservationClient(peer)));
#else
	(void)path; (void)expected_supervisor_uid;
	return Result<std::unique_ptr<UnixTouchObservationClient>>::failure(socket_error(ErrorCode::PlatformUnsupported, "connect"));
#endif
}
Result<std::optional<NormalizedTouchObservation>> UnixTouchObservationClient::try_receive() {
	using Receive = Result<std::optional<NormalizedTouchObservation>>;
	if (peer_ < 0) return Receive::failure(socket_error(ErrorCode::UiDisconnected, "receive"));
	if (buffered_.find('\n') == std::string::npos) {
		std::array<char, kMaximumTouchFrameBytes> bytes{};
		auto count = ::recv(peer_, bytes.data(), kMaximumTouchFrameBytes - buffered_.size(), MSG_DONTWAIT);
		if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return Receive::success(std::nullopt);
		if (count <= 0) { disconnect(); return Receive::failure(socket_error(ErrorCode::UiDisconnected, "receive.read")); }
		buffered_.append(bytes.data(), static_cast<std::size_t>(count));
	}
	auto end = buffered_.find('\n');
	if (end == std::string::npos) {
		if (buffered_.size() < kMaximumTouchFrameBytes) return Receive::success(std::nullopt);
		disconnect(); return Receive::failure(socket_error(ErrorCode::WireInvalid, "receive.frame_bound"));
	}
	auto value = decode_touch_observation(std::string_view(buffered_).substr(0, end + 1));
	buffered_.erase(0, end + 1);
	if (!value) { disconnect(); return Receive::failure(value.error()); }
	if (value.value().observation_sequence <= latest_sequence_) {
		disconnect(); return Receive::failure(socket_error(ErrorCode::ObservationSequenceNonmonotonic, "receive.sequence"));
	}
	latest_sequence_ = value.value().observation_sequence;
	return Receive::success(std::move(value).value());
}
}
