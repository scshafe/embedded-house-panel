// bhp-reporter: the panel's bug reporter (bug-reports-and-self-repair.md, step 1).
// Runs under its own UID. Serves the UI's socket (--ui-socket, exact --ui-uid)
// with the reporter protocol and the services' log tees on --ingest-socket
// (peers from --ingest-uids only). Keeps their logs in RAM, faults in
// --fault-dir, bundles in --outbox-dir, and sends them to the PC's inbox at
// --inbox-url over the tailnet. It opens no audio device and holds no token.
#include "bhp/http_runtime/http_request.h"
#include "bhp/media_window_protocol/media_window_protocol.h"
#include "bhp/structured_logger.h"
#include "bug_reporter/reporter_service.h"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <ctime>
#include <iostream>
#include <memory>
#include <poll.h>
#include <set>
#include <sstream>
#include <string>
#include <sys/random.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <vector>

namespace
{
using namespace bug_reporter;
namespace wire = bug_reporter::protocol;
volatile std::sig_atomic_t stopping = 0;
void request_stop(int) { stopping = 1; }
constexpr std::size_t kMaximumIngestPeers = 32;
constexpr std::size_t kMaximumIngestLine = 32 * 1024;

const char *kUsage =
	"usage: bhp-reporter --ui-socket PATH --ui-uid UID --ingest-socket PATH --ingest-uids UID[,UID...]\n"
	"  --outbox-dir DIR --fault-dir DIR --inbox-url URL [--build-info FILE]\n"
	"  [--handoff-dir DIR --description-channel N]\n";

bool random_bytes(unsigned char *bytes, std::size_t size)
{
	std::size_t filled = 0;
	while (filled < size) {
		const auto got = getrandom(bytes + filled, size - filled, 0);
		if (got < 0 && errno == EINTR)
			continue;
		if (got <= 0)
			return false;
		filled += static_cast<std::size_t>(got);
	}
	return true;
}

double random01()
{
	std::uint32_t value = 0;
	return random_bytes(reinterpret_cast<unsigned char *>(&value), sizeof value) ? value / 4294967296.0 : 0.5;
}

std::optional<std::uint32_t> uid_of(const std::string &text)
{
	if (text.empty() || text.size() > 10 || text.find_first_not_of("0123456789") != std::string::npos)
		return std::nullopt;
	const auto value = std::stoull(text);
	if (value == 0 || value > 0xffffffffULL)
		return std::nullopt;
	return static_cast<std::uint32_t>(value);
}

/** Binds a stream socket (mode given) in an existing directory; a stale path is refused, not removed. */
int listen_on(const std::string &path, mode_t mode, int backlog)
{
	if (path.empty() || path[0] != '/' || path.size() >= sizeof(sockaddr_un::sun_path))
		return -1;
	const int listener = ::socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
	sockaddr_un address{};
	address.sun_family = AF_UNIX;
	std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
	if (listener < 0 || ::bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof address) != 0 ||
	    ::chmod(path.c_str(), mode) != 0 || ::listen(listener, backlog) != 0) {
		if (listener >= 0)
			::close(listener);
		return -1;
	}
	return listener;
}

bool send_event(bhp::media_window_protocol::FrameChannel &channel, const wire::ReporterEvent &event)
{
	auto frame = wire::encode(event);
	return frame.has_value() && channel.send(frame.value()).has_value();
}

struct IngestPeer {
	std::unique_ptr<bhp::media_window_protocol::FrameChannel> channel;
	std::string service;
};
} // namespace

int main(int argc, char **argv)
{
	bhp::StructuredLogger logger("bug_reporter.bhp_reporter", std::cerr);
	const std::string trace = "bug-reporter";
	std::string ui_socket, ingest_socket, outbox_dir, fault_dir, inbox_url, build_info, handoff_dir;
	unsigned description_channel = 0;
	std::optional<std::uint32_t> ui_uid;
	std::set<std::uint32_t> ingest_uids;
	bool usage = false;
	for (int at = 1; at < argc && !usage; ++at) {
		const std::string option = argv[at];
		if (at + 1 >= argc) {
			usage = true;
			break;
		}
		const std::string value = argv[++at];
		if (option == "--ui-socket")
			ui_socket = value;
		else if (option == "--ui-uid")
			ui_uid = uid_of(value);
		else if (option == "--ingest-socket")
			ingest_socket = value;
		else if (option == "--ingest-uids") {
			std::stringstream list(value);
			for (std::string item; std::getline(list, item, ',');) {
				const auto uid = uid_of(item);
				if (!uid)
					usage = true;
				else
					ingest_uids.insert(*uid);
			}
		} else if (option == "--outbox-dir")
			outbox_dir = value;
		else if (option == "--fault-dir")
			fault_dir = value;
		else if (option == "--inbox-url")
			inbox_url = value;
		else if (option == "--build-info")
			build_info = value;
		else if (option == "--handoff-dir")
			handoff_dir = value;
		else if (option == "--description-channel" && value.size() == 1 && value[0] >= '0' && value[0] <= '7')
			description_channel = static_cast<unsigned>(value[0] - '0');
		else
			usage = true;
	}
	if (usage || ui_socket.empty() || !ui_uid || ingest_socket.empty() || ingest_uids.empty() || outbox_dir.empty() ||
	    fault_dir.empty() || (inbox_url.rfind("https://", 0) != 0 && inbox_url.rfind("http://127.0.0.1:", 0) != 0) ||
	    *ui_uid == ::geteuid()) {
		std::cerr << kUsage;
		return 2;
	}
	std::signal(SIGINT, request_stop);
	std::signal(SIGTERM, request_stop);
	std::signal(SIGPIPE, SIG_IGN);
	::umask(077);
	if (::mkdir(outbox_dir.c_str(), 0700) != 0 && errno != EEXIST) {
		logger.log_error("reporter_state_unavailable", trace, "REPORT.STORAGE_UNAVAILABLE",
				 "Cannot create the outbox directory", std::to_string(errno));
		return 1;
	}
	auto created = bhp::http_runtime::HttpRuntime::create();
	if (!created.has_value()) {
		logger.log_error("reporter_http_unavailable", trace, created.error().code, created.error().message, "");
		return 1;
	}
	auto runtime = created.value();
	ReporterSettings settings;
	settings.outbox_directory = outbox_dir;
	settings.fault_directory = fault_dir;
	settings.inbox.base_url = inbox_url;
	if (!build_info.empty())
		settings.build_info_path = build_info;
	settings.handoff_directory = handoff_dir;
	settings.description_channel = description_channel;
	ReporterPorts ports;
	ports.transport = [runtime](const bhp::http_runtime::HttpRequest &request) {
		return bhp::http_runtime::perform(*runtime, request, runtime->cancellation());
	};
	ports.steady_now = [] { return std::chrono::steady_clock::now(); };
	ports.unix_now = [] { return static_cast<std::int64_t>(std::time(nullptr)); };
	ports.random01 = random01;
	ports.random_bytes = random_bytes;
	auto opened = ReporterService::open(settings, ports);
	if (!opened.has_value()) {
		logger.log_error("reporter_unavailable", trace, opened.error().code, opened.error().message, "");
		return 1;
	}
	auto &service = *opened.value();
	const int ui_listener = listen_on(ui_socket, 0660, 2);
	const int ingest_listener = listen_on(ingest_socket, 0660, 16);
	if (ui_listener < 0 || ingest_listener < 0) {
		logger.log_error("reporter_socket_unavailable", trace, "REPORT.SOCKET_UNAVAILABLE",
				 "Cannot listen on the UI or ingest socket", std::to_string(errno));
		return 1;
	}
	logger.log(bhp::StructuredLogLevel::info, "reporter_started", trace);
	std::unique_ptr<bhp::media_window_protocol::FrameChannel> ui;
	std::vector<IngestPeer> peers;
	std::uint64_t sent_sequence = 0;
	std::string logged_error;
	auto due = std::chrono::steady_clock::now();
	while (!stopping) {
		// New lines or requests may file a report, so the service looks again at once.
		bool active = false;
		for (int peer; (peer = ::accept4(ui_listener, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC)) >= 0;) {
			const auto uid = bhp::media_window_protocol::peer_uid(peer);
			if (!uid || *uid != *ui_uid || (ui && ui->open())) {
				::close(peer);
				logger.log(bhp::StructuredLogLevel::warn, "reporter_ui_refused", trace,
					   {{"reason", !uid || *uid != *ui_uid ? "peer_uid" : "already_attached"}});
				continue;
			}
			ui = std::make_unique<bhp::media_window_protocol::FrameChannel>(peer, wire::kMaximumFrameBytes);
			sent_sequence = send_event(*ui, service.reports()) ? service.reports_sequence() : 0;
		}
		for (int peer; (peer = ::accept4(ingest_listener, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC)) >= 0;) {
			const auto uid = bhp::media_window_protocol::peer_uid(peer);
			if (!uid || !ingest_uids.count(*uid) || peers.size() >= kMaximumIngestPeers) {
				::close(peer);
				continue;
			}
			peers.push_back({std::make_unique<bhp::media_window_protocol::FrameChannel>(peer, kMaximumIngestLine), {}});
		}
		for (auto &peer : peers) {
			for (int lines = 0; lines < 256 && peer.channel->open(); ++lines) {
				auto received = peer.channel->receive();
				if (!received || !received.value())
					break;
				active = true;
				auto line = *received.value();
				if (!line.empty() && line.back() == '\n')
					line.pop_back();
				const auto classified = classify(line);
				if (const auto *hello = std::get_if<HelloLine>(&classified)) {
					if (peer.service.empty())
						peer.service = hello->service;
				} else if (peer.service.empty())
					peer.channel->close(); // a tee says who it is first
				else if (const auto *exit = std::get_if<ExitLine>(&classified))
					service.exited(*exit);
				else
					service.ingest(peer.service, line);
			}
		}
		peers.erase(std::remove_if(peers.begin(), peers.end(), [](const IngestPeer &p) { return !p.channel->open(); }),
			    peers.end());
		while (ui && ui->open()) {
			auto received = ui->receive();
			if (!received || !received.value())
				break;
			auto request = wire::decode_ui_request(*received.value());
			if (!request.has_value()) {
				ui->close();
				break;
			}
			active = true;
			for (const auto &event : service.handle(request.value())) {
				if (std::holds_alternative<wire::Reports>(event))
					sent_sequence = service.reports_sequence();
				(void)send_event(*ui, event);
			}
		}
		const auto now = std::chrono::steady_clock::now();
		if (active || now >= due)
			due = now + service.tick();
		// Codes only: no description, log line or address reaches this log.
		if (service.last_error() != logged_error) {
			logged_error = service.last_error();
			logger.log(bhp::StructuredLogLevel::warn, "reporter_failed", trace, {{"error_code", logged_error}});
		}
		if (ui && ui->open() && service.reports_sequence() != sent_sequence && send_event(*ui, service.reports()))
			sent_sequence = service.reports_sequence();
		std::vector<pollfd> descriptors{{ui_listener, POLLIN, 0}, {ingest_listener, POLLIN, 0},
						{ui && ui->open() ? ui->descriptor() : -1, POLLIN, 0}};
		for (const auto &peer : peers)
			descriptors.push_back({peer.channel->descriptor(), POLLIN, 0});
		const auto wait = std::chrono::duration_cast<std::chrono::milliseconds>(due - std::chrono::steady_clock::now());
		::poll(descriptors.data(), descriptors.size(), static_cast<int>(std::clamp<long long>(wait.count(), 10, 1000)));
	}
	::close(ui_listener);
	::close(ingest_listener);
	::unlink(ui_socket.c_str());
	::unlink(ingest_socket.c_str());
	(void)runtime->shutdown();
	logger.log(bhp::StructuredLogLevel::info, "reporter_stopped", trace);
	return 0;
}
