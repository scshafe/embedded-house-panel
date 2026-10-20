// bhp-voice-lab-uploader: the voice-lab uploader (recorder wire plan Step 7,
// ADR 0012). Runs under its own UID, the only holder of the voice-lab refresh
// token; serves the UI's socket (--ui-socket, exact --ui-uid) with the
// uploader protocol and runs UploaderService: sign-in, the D-021 approval,
// the catalog and library, and the outbox of takes the broker hands off
// (--handoff-dir). The PC's node is resolved through `tailscale status --json`.
#include "bhp/http_runtime/http_request.h"
#include "bhp/media_window_protocol/media_window_protocol.h"
#include "bhp/structured_logger.h"
#include "voice_lab_recorder/corpus_client.h"
#include "voice_lab_recorder/uploader_protocol.h"
#include "voice_lab_recorder/uploader_service.h"
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <poll.h>
#include <sstream>
#include <spawn.h>
#include <string>
#include <sys/random.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

namespace
{
using namespace voice_lab_recorder;
namespace wire = voice_lab_recorder::uploader_protocol;
using Json = nlohmann::json;
volatile std::sig_atomic_t stopping = 0;
void request_stop(int) { stopping = 1; }

const char *kUsage =
	"usage: bhp-voice-lab-uploader --ui-socket PATH --ui-uid UID --state-dir DIR --handoff-dir DIR\n"
	"  --issuer https://HOST --api-url https://HOST --endpoint HOST --tailnet-suffix NAME\n"
	"  --board-profile FILE [--client-version TEXT] [--tailscale PATH] [--ca-file PEM]\n";

std::string utc_now()
{
	const std::time_t now = std::time(nullptr);
	std::tm parts{};
	gmtime_r(&now, &parts);
	char text[32];
	std::strftime(text, sizeof text, "%Y-%m-%dT%H:%M:%SZ", &parts);
	return text;
}

double random01()
{
	std::uint32_t value = 0;
	while (getrandom(&value, sizeof value, 0) != static_cast<ssize_t>(sizeof value))
		if (errno != EINTR)
			std::abort();
	return static_cast<double>(value) / 4294967296.0;
}

std::optional<std::string> read_file(const std::string &path)
{
	std::ifstream input(path, std::ios::binary);
	if (!input)
		return std::nullopt;
	std::ostringstream contents;
	contents << input.rdbuf();
	return contents.str();
}

/** `tailscale status --json`, read with a deadline; the peer whose name is the endpoint. */
Result<PeerIdentity> tailscale_peer(const std::string &tailscale, const std::string &endpoint)
{
	auto unavailable = [] {
		return Result<PeerIdentity>::failure(
			{"VOICE_LAB.TAILNET_UNAVAILABLE", "tailscaled did not report the PC's node", {}, true});
	};
	int pipe_fds[2];
	if (::pipe2(pipe_fds, O_CLOEXEC) != 0)
		return unavailable();
	posix_spawn_file_actions_t actions;
	posix_spawn_file_actions_init(&actions);
	posix_spawn_file_actions_adddup2(&actions, pipe_fds[1], STDOUT_FILENO);
	posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
	char *const argv[] = {const_cast<char *>(tailscale.c_str()), const_cast<char *>("status"),
			      const_cast<char *>("--json"), nullptr};
	char *const env[] = {const_cast<char *>("PATH=/usr/sbin:/usr/bin:/sbin:/bin"), nullptr};
	pid_t child = -1;
	const int spawned = posix_spawn(&child, tailscale.c_str(), &actions, nullptr, argv, env);
	posix_spawn_file_actions_destroy(&actions);
	::close(pipe_fds[1]);
	std::string output;
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (spawned == 0 && output.size() < (4u << 20)) {
		const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
		if (left.count() <= 0)
			break;
		pollfd readable{pipe_fds[0], POLLIN, 0};
		if (::poll(&readable, 1, static_cast<int>(left.count())) <= 0)
			break;
		char buffer[65536];
		const ssize_t got = ::read(pipe_fds[0], buffer, sizeof buffer);
		if (got <= 0)
			break;
		output.append(buffer, static_cast<std::size_t>(got));
	}
	::close(pipe_fds[0]);
	int status = 0;
	if (spawned == 0) {
		::kill(child, SIGKILL);
		::waitpid(child, &status, 0);
	}
	const Json document = Json::parse(output, nullptr, false);
	if (spawned != 0 || !document.is_object() || !document.contains("Peer") || !document["Peer"].is_object())
		return unavailable();
	for (const auto &[key, peer] : document["Peer"].items()) {
		std::string name = peer.value("DNSName", std::string());
		if (!name.empty() && name.back() == '.')
			name.pop_back();
		if (name != endpoint)
			continue;
		PeerIdentity identity{peer.value("ID", std::string()), {}, peer.value("DNSName", std::string())};
		for (const auto &tag : peer.value("Tags", Json::array()))
			if (tag.is_string())
				identity.tags.push_back(tag.get<std::string>());
		return Result<PeerIdentity>::success(identity);
	}
	return unavailable();
}

/** Binds the UI socket (0660) in an existing directory; a stale path is refused, not removed. */
int listen_ui(const std::string &path)
{
	if (path.empty() || path[0] != '/' || path.size() >= sizeof(sockaddr_un::sun_path))
		return -1;
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

bool send_event(bhp::media_window_protocol::FrameChannel &channel, const wire::UploaderEvent &event)
{
	auto frame = wire::encode(event);
	return frame.has_value() && channel.send(frame.value()).has_value();
}
} // namespace

int main(int argc, char **argv)
{
	bhp::StructuredLogger logger("voice_lab_recorder.bhp_voice_lab_uploader", std::cerr);
	const std::string trace = "voice-lab-uploader";
	std::string ui_socket, state_dir, handoff_dir, issuer, api_url, endpoint, suffix, profile_path, ca_file;
	std::string client_version = "0.0.0", tailscale = "/usr/bin/tailscale";
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
			 value.size() < 11)
			ui_uid = std::stoull(value);
		else if (option == "--state-dir")
			state_dir = value;
		else if (option == "--handoff-dir")
			handoff_dir = value;
		else if (option == "--issuer")
			issuer = value;
		else if (option == "--api-url")
			api_url = value;
		else if (option == "--endpoint")
			endpoint = value;
		else if (option == "--tailnet-suffix")
			suffix = value;
		else if (option == "--board-profile")
			profile_path = value;
		else if (option == "--client-version")
			client_version = value;
		else if (option == "--tailscale")
			tailscale = value;
		else if (option == "--ca-file")
			ca_file = value;
		else {
			std::cerr << kUsage;
			return 2;
		}
	}
	if (ui_socket.empty() || ui_uid == 0 || ui_uid > 0xffffffffULL || state_dir.empty() || handoff_dir.empty() ||
	    issuer.rfind("https://", 0) != 0 || api_url.rfind("https://", 0) != 0 || endpoint.empty() ||
	    suffix.empty() || profile_path.empty() || ui_uid == static_cast<unsigned long long>(::geteuid())) {
		std::cerr << kUsage;
		return 2;
	}
	const auto profile_text = read_file(profile_path);
	const Json profile = profile_text ? Json::parse(*profile_text, nullptr, false) : Json();
	if (!profile.is_object() || !profile.contains("channels") || !profile["channels"].is_array() ||
	    !profile.contains("room") || !profile["room"].is_string() || !profile.contains("primary_channel") ||
	    !profile["primary_channel"].is_number_unsigned() || !profile.contains("capture_host") ||
	    !profile["capture_host"].is_string()) {
		logger.log_error("voice_lab_board_profile_invalid", trace, "VOICE_LAB.BOARD_PROFILE_INVALID",
				 "The board profile needs channels, room, primary_channel and capture_host", "");
		return 2;
	}
	std::string ca;
	if (!ca_file.empty()) {
		auto read = read_file(ca_file);
		if (!read) {
			std::cerr << kUsage;
			return 2;
		}
		ca = *read;
	}
	std::signal(SIGINT, request_stop);
	std::signal(SIGTERM, request_stop);
	std::signal(SIGPIPE, SIG_IGN);
	::umask(077);
	for (const char *name : {"", "/token", "/outbox", "/home-host"}) {
		const std::string directory = state_dir + name;
		if (::mkdir(directory.c_str(), 0700) != 0 && errno != EEXIST) {
			logger.log_error("voice_lab_state_unavailable", trace, "VOICE_LAB.STATE_UNAVAILABLE",
					 "Cannot create the state directory", std::to_string(errno));
			return 1;
		}
	}

	auto created = bhp::http_runtime::HttpRuntime::create();
	if (!created.has_value()) {
		logger.log_error("voice_lab_http_unavailable", trace, created.error().code, created.error().message, "");
		return 1;
	}
	auto runtime = created.value();
	UploaderSettings settings;
	settings.sign_in.issuer = issuer;
	settings.sign_in.trusted_ca_pem_for_tests = ca;
	settings.corpus.api_url = api_url;
	settings.corpus.trusted_ca_pem_for_tests = ca;
	settings.token_directory = state_dir + "/token";
	settings.outbox.handoff_directory = handoff_dir;
	settings.outbox.outbox_directory = state_dir + "/outbox";
	settings.outbox.home_host_directory = state_dir + "/home-host";
	settings.outbox.channels = profile["channels"];
	settings.outbox.room = profile["room"];
	settings.outbox.primary_channel = profile["primary_channel"];
	settings.outbox.capture_host = profile["capture_host"];
	settings.outbox.client = {{"name", "embedded-house-panel"}, {"version", client_version}, {"platform", "Linux aarch64"}};
	settings.approval_endpoint = endpoint;
	settings.tailnet_suffix = suffix;
	UploaderPorts ports;
	ports.transport = [runtime](const bhp::http_runtime::HttpRequest &request) {
		return bhp::http_runtime::perform(*runtime, request, runtime->cancellation());
	};
	ports.resolve = [tailscale](const std::string &host) { return tailscale_peer(tailscale, host); };
	ports.utc_now = utc_now;
	ports.steady_now = [] { return std::chrono::steady_clock::now(); };
	ports.random01 = random01;
	ports.request_id = new_request_id;
	auto opened = UploaderService::open(settings, ports);
	if (!opened.has_value()) {
		logger.log_error("voice_lab_uploader_unavailable", trace, opened.error().code, opened.error().message, "");
		return 1;
	}
	auto &service = *opened.value();
	const int listener = listen_ui(ui_socket);
	if (listener < 0) {
		logger.log_error("voice_lab_ui_socket_unavailable", trace, "VOICE_LAB.UI_SOCKET_UNAVAILABLE",
				 "Cannot listen on the UI socket", std::to_string(errno));
		return 1;
	}
	logger.log(bhp::StructuredLogLevel::info, "voice_lab_uploader_started", trace);
	std::unique_ptr<bhp::media_window_protocol::FrameChannel> ui;
	std::uint64_t sent_sequence = 0;
	auto due = std::chrono::steady_clock::now();
	while (!stopping) {
		const int peer = ::accept4(listener, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
		if (peer >= 0) {
			const auto uid = bhp::media_window_protocol::peer_uid(peer);
			if (!uid || *uid != ui_uid || (ui && ui->open())) {
				::close(peer);
				logger.log(bhp::StructuredLogLevel::warn, "voice_lab_ui_refused", trace,
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
				(void)send_event(*ui, event);
			}
		}
		const auto now = std::chrono::steady_clock::now();
		if (now >= due)
			due = now + service.tick();
		if (ui && ui->open()) {
			const auto state = service.state();
			if (state.sequence != sent_sequence && send_event(*ui, state))
				sent_sequence = state.sequence;
		}
		pollfd descriptors[2] = {{listener, POLLIN, 0}, {ui && ui->open() ? ui->descriptor() : -1, POLLIN, 0}};
		const auto wait = std::chrono::duration_cast<std::chrono::milliseconds>(due - std::chrono::steady_clock::now());
		::poll(descriptors, 2, static_cast<int>(std::clamp<long long>(wait.count(), 10, 1000)));
	}
	::close(listener);
	::unlink(ui_socket.c_str());
	(void)runtime->shutdown();
	logger.log(bhp::StructuredLogLevel::info, "voice_lab_uploader_stopped", trace);
	return 0;
}
