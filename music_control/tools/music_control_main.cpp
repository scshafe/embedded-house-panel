// bhp-music-control: the panel's Spotify Connect remote (ADR 0013). Runs under
// its own UID, the only holder of the Spotify refresh token
// (<state-dir>/token/spotify-token.json, put there by the owner's one-time
// sign-in); serves the UI's socket (--ui-socket, exact --ui-uid) with the
// music-control protocol and runs MusicService for the one speaker named by
// --output-name. It sends Spotify Web API commands only and opens no audio device.
#include "bhp/http_runtime/http_request.h"
#include "bhp/unix_socket_path/stale_socket.h"
#include "bhp/media_window_protocol/media_window_protocol.h"
#include "bhp/structured_logger.h"
#include "music_control/music_protocol.h"
#include "music_control/music_service.h"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <iostream>
#include <memory>
#include <poll.h>
#include <string>
#include <sys/random.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace
{
using namespace music_control;
namespace wire = music_control::protocol;
volatile std::sig_atomic_t stopping = 0;
void request_stop(int) { stopping = 1; }

const char *kUsage = "usage: bhp-music-control --ui-socket PATH --ui-uid UID --state-dir DIR\n"
		     "  --client-id ID --output-name NAME\n"
		     "  NAME is the speaker's Spotify device name, exactly as the Spotify app shows it.\n";

double random01()
{
	std::uint32_t value = 0;
	while (getrandom(&value, sizeof value, 0) != static_cast<ssize_t>(sizeof value))
		if (errno != EINTR)
			std::abort();
	return static_cast<double>(value) / 4294967296.0;
}

/** Binds the UI socket (0660) in an existing directory, replacing only a stale socket of ours (unix_socket_path). */
int listen_ui(const std::string &path)
{
	if (path.empty() || path[0] != '/' || path.size() >= sizeof(sockaddr_un::sun_path))
		return -1;
	(void)bhp::unix_socket_path::clear_stale_socket(path);
	const int listener = ::socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
	sockaddr_un address{};
	address.sun_family = AF_UNIX;
	std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
	if (listener < 0 || ::bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof address) != 0 ||
	    ::chmod(path.c_str(), 0660) != 0 || ::listen(listener, 2) != 0) {
		if (listener >= 0)
			::close(listener);
		return -1;
	}
	return listener;
}

bool send_event(bhp::media_window_protocol::FrameChannel &channel, const wire::MusicEvent &event)
{
	auto frame = wire::encode(event);
	return frame.has_value() && channel.send(frame.value()).has_value();
}
} // namespace

int main(int argc, char **argv)
{
	bhp::StructuredLogger logger("music_control.bhp_music_control", std::cerr);
	const std::string trace = "music-control";
	std::string ui_socket, state_dir, client_id, output_name;
	unsigned long long ui_uid = 0;
	for (int at = 1; at < argc; ++at) {
		const std::string option = argv[at];
		if (at + 1 >= argc) {
			std::cerr << kUsage;
			return 2;
		}
		const std::string value = argv[++at];
		if (option == "--ui-socket")
			ui_socket = value;
		else if (option == "--ui-uid" && value.find_first_not_of("0123456789") == std::string::npos &&
			 !value.empty() && value.size() < 11)
			ui_uid = std::stoull(value);
		else if (option == "--state-dir")
			state_dir = value;
		else if (option == "--client-id")
			client_id = value;
		else if (option == "--output-name")
			output_name = value;
		else {
			std::cerr << kUsage;
			return 2;
		}
	}
	if (ui_socket.empty() || ui_uid == 0 || ui_uid > 0xffffffffULL || state_dir.empty() ||
	    !valid_client_id(client_id) || output_name.empty() || ui_uid == static_cast<unsigned long long>(::geteuid())) {
		std::cerr << kUsage;
		return 2;
	}
	std::signal(SIGINT, request_stop);
	std::signal(SIGTERM, request_stop);
	std::signal(SIGPIPE, SIG_IGN);
	::umask(077);
	for (const char *name : {"", "/token"}) {
		const std::string directory = state_dir + name;
		if (::mkdir(directory.c_str(), 0700) != 0 && errno != EEXIST) {
			logger.log_error("music_state_unavailable", trace, "MUSIC.STATE_UNAVAILABLE",
					 "Cannot create the state directory", std::to_string(errno));
			return 1;
		}
	}

	auto created = bhp::http_runtime::HttpRuntime::create();
	if (!created.has_value()) {
		logger.log_error("music_http_unavailable", trace, created.error().code, created.error().message, "");
		return 1;
	}
	auto runtime = created.value();
	MusicSettings settings;
	settings.auth.client_id = client_id;
	settings.token_directory = state_dir + "/token";
	settings.output_name = output_name;
	MusicPorts ports;
	ports.transport = [runtime](const bhp::http_runtime::HttpRequest &request) {
		return bhp::http_runtime::perform(*runtime, request, runtime->cancellation());
	};
	ports.steady_now = [] { return std::chrono::steady_clock::now(); };
	ports.random01 = random01;
	auto opened = MusicService::open(settings, ports);
	if (!opened.has_value()) {
		logger.log_error("music_control_unavailable", trace, opened.error().code, opened.error().message, "");
		return 2;
	}
	auto &service = *opened.value();
	const int listener = listen_ui(ui_socket);
	if (listener < 0) {
		logger.log_error("music_ui_socket_unavailable", trace, "MUSIC.UI_SOCKET_UNAVAILABLE",
				 "Cannot listen on the UI socket", std::to_string(errno));
		return 1;
	}
	logger.log(bhp::StructuredLogLevel::info, "music_control_started", trace,
		   {{"signed_in", service.state().sign_in == "signed_in" ? "true" : "false"}});
	std::unique_ptr<bhp::media_window_protocol::FrameChannel> ui;
	std::uint64_t sent_sequence = 0;
	std::string logged_error;
	auto due = std::chrono::steady_clock::now();
	while (!stopping) {
		const int peer = ::accept4(listener, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
		if (peer >= 0) {
			const auto uid = bhp::media_window_protocol::peer_uid(peer);
			if (!uid || *uid != ui_uid || (ui && ui->open())) {
				::close(peer);
				logger.log(bhp::StructuredLogLevel::warn, "music_ui_refused", trace,
					   {{"reason", !uid || *uid != ui_uid ? "peer_uid" : "already_attached"}});
			} else {
				ui = std::make_unique<bhp::media_window_protocol::FrameChannel>(peer, wire::kMaximumFrameBytes);
				const auto state = service.state();
				sent_sequence = send_event(*ui, state) ? state.sequence : 0;
			}
		}
		while (ui && ui->open()) {
			auto received = ui->receive();
			if (!received || !received.value())
				break;
			auto request = wire::decode_ui_request(*received.value());
			if (!request.has_value()) {
				ui->close();
				break;
			}
			for (const auto &event : service.handle(request.value())) {
				if (const auto *state = std::get_if<wire::State>(&event))
					sent_sequence = state->sequence;
				else if (const auto *refused = std::get_if<wire::Refused>(&event))
					logger.log(bhp::StructuredLogLevel::warn, "music_request_refused", trace,
						   {{"request", refused->request}, {"error_code", refused->error_code}});
				(void)send_event(*ui, event);
			}
		}
		const auto now = std::chrono::steady_clock::now();
		if (now >= due)
			due = now + service.tick();
		const auto state = service.state();
		// Codes only: no token, device ID, track or speaker name reaches the log.
		if (state.error != logged_error) {
			logged_error = state.error;
			if (!logged_error.empty())
				logger.log(bhp::StructuredLogLevel::warn, "music_spotify_failed", trace, {{"error_code", logged_error}});
		}
		if (ui && ui->open() && state.sequence != sent_sequence && send_event(*ui, state))
			sent_sequence = state.sequence;
		pollfd descriptors[2] = {{listener, POLLIN, 0}, {ui && ui->open() ? ui->descriptor() : -1, POLLIN, 0}};
		const auto wait = std::chrono::duration_cast<std::chrono::milliseconds>(due - std::chrono::steady_clock::now());
		::poll(descriptors, 2, static_cast<int>(std::clamp<long long>(wait.count(), 10, 1000)));
	}
	::close(listener);
	::unlink(ui_socket.c_str());
	(void)runtime->shutdown();
	logger.log(bhp::StructuredLogLevel::info, "music_control_stopped", trace);
	return 0;
}
