#include "panel_supervisor/media_window_endpoint.hpp"
#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace house_panel::supervisor {
namespace {
namespace wire = bhp::media_window_protocol;

SupervisorError window_error(ErrorCode code, const char *operation, std::string detail = {}) {
	std::map<std::string, std::string> context{{"operation", operation}};
	if (!detail.empty()) context.emplace("detail", std::move(detail));
	return SupervisorError(code, "Media window endpoint operation failed", std::move(context));
}

bool trusted_parent(const std::string &path) {
	if (path.empty() || path[0] != '/' || path.size() >= sizeof(sockaddr_un::sun_path)
		|| path.find("//") != std::string::npos || path.find("/../") != std::string::npos
		|| path.find("/./") != std::string::npos || path.back() == '/') return false;
	for (std::size_t end = 1; end != std::string::npos; end = path.find('/', end + 1)) {
		const auto parent = end == 1 ? std::string("/") : path.substr(0, end);
		struct stat info{};
		if (::lstat(parent.c_str(), &info) != 0 || !S_ISDIR(info.st_mode)
			|| info.st_uid != 0 || (info.st_mode & 0022) != 0) return false;
	}
	return true;
}
}

MediaWindowEndpoint::MediaWindowEndpoint(std::string path, int listener, std::uint32_t broker_uid,
	std::string supervisor_session_id)
	: path_(std::move(path)), listener_(listener), broker_uid_(broker_uid),
	  session_(std::move(supervisor_session_id)) {}

MediaWindowEndpoint::~MediaWindowEndpoint() {
	drop();
	if (listener_ >= 0) {
		::close(listener_);
		::unlink(path_.c_str());
	}
}

Result<std::unique_ptr<MediaWindowEndpoint>> MediaWindowEndpoint::listen(
	std::string path, std::uint32_t broker_uid, std::string supervisor_session_id) {
	using Output = Result<std::unique_ptr<MediaWindowEndpoint>>;
	if (broker_uid == 0 || ::geteuid() != 0 || !trusted_parent(path))
		return Output::failure(window_error(ErrorCode::MediaWindowUnavailable, "listen.validate"));
	const int listener = ::socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
	if (listener < 0) return Output::failure(window_error(ErrorCode::MediaWindowUnavailable, "listen.socket"));
	sockaddr_un address{};
	address.sun_family = AF_UNIX;
	std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
	// The socket takes its group from its runtime directory (root:<broker group>
	// 0750), so only the broker's group can reach it; the UID check still decides.
	struct stat parent{};
	const auto slash = path.rfind('/');
	if (::lstat(slash == 0 ? "/" : path.substr(0, slash).c_str(), &parent) != 0
		|| ::bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0
		|| ::chown(path.c_str(), 0, parent.st_gid) != 0
		|| ::chmod(path.c_str(), 0660) != 0 || ::listen(listener, 2) != 0) {
		const auto error = window_error(ErrorCode::MediaWindowUnavailable, "listen.bind",
			std::to_string(errno));
		::close(listener);
		return Output::failure(error);
	}
	return Output::success(std::unique_ptr<MediaWindowEndpoint>(new MediaWindowEndpoint(
		std::move(path), listener, broker_uid, std::move(supervisor_session_id))));
}

Result<std::unique_ptr<MediaWindowEndpoint>> MediaWindowEndpoint::for_connected_peer(
	int descriptor, std::uint32_t broker_uid, std::string supervisor_session_id) {
	using Output = Result<std::unique_ptr<MediaWindowEndpoint>>;
	std::unique_ptr<MediaWindowEndpoint> endpoint(
		new MediaWindowEndpoint({}, -1, broker_uid, std::move(supervisor_session_id)));
	auto adopted = endpoint->adopt(descriptor);
	if (!adopted) return Output::failure(adopted.error());
	return Output::success(std::move(endpoint));
}

bool MediaWindowEndpoint::connected() const noexcept { return peer_ && peer_->open(); }

void MediaWindowEndpoint::drop() noexcept { peer_.reset(); }

Result<void> MediaWindowEndpoint::adopt(int descriptor) {
	const auto uid = wire::peer_uid(descriptor);
	if (!uid || *uid != broker_uid_) {
		::close(descriptor);
		return Result<void>::failure(window_error(ErrorCode::MediaWindowUnavailable, "adopt.peer_uid"));
	}
	peer_ = std::make_unique<wire::FrameChannel>(descriptor);
	return send_window();
}

Result<void> MediaWindowEndpoint::send_window() {
	if (!connected()) return Result<void>::success();
	auto frame = wire::encode(wire::WindowMessage{session_, ++sequence_, open_, reason_});
	if (!frame) {
		drop();
		return Result<void>::failure(window_error(ErrorCode::MediaWindowProtocolFailed, "send_window.encode"));
	}
	if (!peer_->send(frame.value())) {
		drop();
		return Result<void>::failure(window_error(ErrorCode::MediaWindowUnavailable, "send_window.send"));
	}
	return Result<void>::success();
}

void MediaWindowEndpoint::set_window(bool open, wire::WindowReason reason) {
	if (open != (reason == wire::WindowReason::DayPresent)) {
		open = false;
		if (reason == wire::WindowReason::DayPresent) reason = wire::WindowReason::Absence;
	}
	if (open == open_ && reason == reason_) return;
	open_ = open;
	reason_ = reason;
	// A failed send drops the broker, which then treats the window as closed.
	(void)send_window();
}

Result<void> MediaWindowEndpoint::answer(const std::string &frame, const Redeem &redeem) {
	auto request = wire::decode_claim_request(frame);
	if (!request) {
		drop();
		return Result<void>::failure(window_error(ErrorCode::MediaWindowProtocolFailed, "answer.decode"));
	}
	wire::ClaimResult result{session_, 0, request.value().request_id, std::nullopt, {}};
	if (!open_) {
		result.error_code = "window_closed";
	} else {
		TouchObservationClaim claim{request.value().touch_observation_id,
			request.value().canonical_operation_digest, request.value().presentation_binding, std::nullopt};
		auto redeemed = redeem(claim);
		if (redeemed) {
			const auto &envelope = redeemed.value();
			result.envelope = wire::ClaimEnvelope{envelope.touch_observation_id, envelope.observation_sequence,
				envelope.canonical_operation_digest, envelope.presentation_binding,
				static_cast<std::uint64_t>(envelope.supervisor_measured_contact_duration.count())};
		} else {
			const auto reason = redeemed.error().context().find("reason");
			result.error_code = reason != redeemed.error().context().end() ? reason->second : "claim_refused";
		}
	}
	result.sequence = ++sequence_;
	auto encoded = wire::encode(result);
	if (!encoded || !peer_->send(encoded.value())) {
		drop();
		return Result<void>::failure(window_error(ErrorCode::MediaWindowUnavailable, "answer.send"));
	}
	return Result<void>::success();
}

Result<void> MediaWindowEndpoint::pump(const Redeem &redeem) {
	if (listener_ >= 0) {
		for (int attempt = 0; attempt < 2; ++attempt) {
			const int candidate = ::accept4(listener_, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
			if (candidate < 0) {
				if (errno == EINTR) continue;
				break;
			}
			if (connected()) { ::close(candidate); continue; }
			(void)adopt(candidate);
		}
	}
	for (int frames = 0; frames < 4 && connected(); ++frames) {
		auto received = peer_->receive();
		if (!received) {
			drop();
			return Result<void>::failure(window_error(ErrorCode::MediaWindowUnavailable, "pump.receive"));
		}
		if (!received.value()) break;
		auto answered = answer(*received.value(), redeem);
		if (!answered) return answered;
	}
	return Result<void>::success();
}
}
