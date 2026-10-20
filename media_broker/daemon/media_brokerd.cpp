// bhp-media-broker: the panel's one audio owner (voice-take capture plan Step 5).
//
// Listens for the UI on --ui-socket (exact --ui-uid), follows the supervisor's
// media window on --window-socket (exact --supervisor-uid), and records
// voice_take leases into --take-root. --fake-source replaces the device with
// the deterministic paced fake for hosts and the simulator.
#include "media_broker/media_broker.h"
#include "media_broker/xvf3800_control.h"
#if defined(BHP_MEDIA_HAS_ALSA)
#include "media_broker/alsa_capture_source.h"
#endif
#include "bhp/structured_logger.h"
#include <cerrno>
#include <csignal>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <iostream>
#include <poll.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
namespace
{
using namespace media_broker;
namespace window = bhp::media_window_protocol;
volatile std::sig_atomic_t stopping = 0;
void request_stop(int) { stopping = 1; }

const char *kUsage =
	"usage: bhp-media-broker --ui-socket PATH --ui-uid UID --window-socket PATH --supervisor-uid UID\n"
	"  --take-root DIR (--pcm NAME | --fake-source) [--maximum-seconds 1..600] [--tolerance-ns N]\n"
	"  [--maximum-takes N] [--maximum-store-bytes N] [--start-deadline-ms N] [--handoff-dir DIR]\n";

bool parse(const std::string &text, unsigned long long minimum, unsigned long long maximum, unsigned long long &out)
{
	if (text.empty() || text.size() > 19 || text.find_first_not_of("0123456789") != std::string::npos)
		return false;
	out = std::stoull(text);
	return out >= minimum && out <= maximum;
}

std::string utc_now()
{
	const std::time_t now = std::time(nullptr);
	std::tm parts{};
	gmtime_r(&now, &parts);
	char text[32];
	std::strftime(text, sizeof text, "%Y-%m-%dT%H:%M:%SZ", &parts);
	return text;
}

std::string random_id()
{
	unsigned char bytes[16];
	std::size_t filled = 0;
	while (filled < sizeof bytes) {
		const auto got = getrandom(bytes + filled, sizeof bytes - filled, 0);
		if (got < 0 && errno == EINTR)
			continue;
		if (got <= 0)
			std::abort();
		filled += static_cast<std::size_t>(got);
	}
	std::string text = "lease-";
	for (const unsigned char byte : bytes) {
		text += "0123456789abcdef"[byte >> 4];
		text += "0123456789abcdef"[byte & 15];
	}
	return text;
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
} // namespace

int main(int argc, char **argv)
{
	bhp::StructuredLogger logger("media_broker.bhp_media_broker", std::cerr);
	const std::string trace = "media-broker";
	std::string ui_socket, window_socket, take_root, pcm, handoff_dir;
	unsigned long long ui_uid = 0, supervisor_uid = 0, maximum_seconds = 600, tolerance_ns = 4'000'000,
			   maximum_takes = 16, maximum_store_bytes = 4ULL << 30, start_deadline_ms = 1000;
	bool fake = false, have_supervisor_uid = false;
	for (int at = 1; at < argc; ++at) {
		const std::string option = argv[at];
		const bool value = at + 1 < argc;
		bool ok = true;
		if (option == "--fake-source")
			fake = true;
		else if (option == "--ui-socket" && value)
			ui_socket = argv[++at];
		else if (option == "--window-socket" && value)
			window_socket = argv[++at];
		else if (option == "--take-root" && value)
			take_root = argv[++at];
		else if (option == "--handoff-dir" && value)
			handoff_dir = argv[++at];
		else if (option == "--pcm" && value)
			pcm = argv[++at];
		else if (option == "--ui-uid" && value)
			ok = parse(argv[++at], 1, 0xffffffffULL, ui_uid);
		else if (option == "--supervisor-uid" && value)
			ok = have_supervisor_uid = parse(argv[++at], 0, 0xffffffffULL, supervisor_uid);
		else if (option == "--maximum-seconds" && value)
			ok = parse(argv[++at], 1, 600, maximum_seconds);
		else if (option == "--tolerance-ns" && value)
			ok = parse(argv[++at], 1, 4'000'000, tolerance_ns);
		else if (option == "--maximum-takes" && value)
			ok = parse(argv[++at], 1, 1000, maximum_takes);
		else if (option == "--maximum-store-bytes" && value)
			ok = parse(argv[++at], 1ULL << 20, 1ULL << 40, maximum_store_bytes);
		else if (option == "--start-deadline-ms" && value)
			ok = parse(argv[++at], 100, 10000, start_deadline_ms);
		else
			ok = false;
		if (!ok) {
			std::cerr << kUsage;
			return 2;
		}
	}
#if !defined(BHP_MEDIA_HAS_ALSA)
	if (!fake) {
		std::cerr << "bhp-media-broker: built without ALSA; use --fake-source\n" << kUsage;
		return 2;
	}
#endif
	if (ui_socket.empty() || window_socket.empty() || take_root.empty() || ui_uid == 0 || !have_supervisor_uid ||
	    fake == !pcm.empty() || ui_uid == static_cast<unsigned long long>(::geteuid())) {
		std::cerr << kUsage;
		return 2;
	}
	std::signal(SIGINT, request_stop);
	std::signal(SIGTERM, request_stop);
	std::signal(SIGPIPE, SIG_IGN);

	auto store = TakeStore::open(take_root, {static_cast<std::size_t>(maximum_takes), maximum_store_bytes, 16ULL << 20});
	if (!store) {
		logger.log_error("media_broker_store_unavailable", trace, store.error().code, store.error().message, store.error().code);
		return 1;
	}
	BrokerSettings settings;
	settings.request = {fake ? "fake" : pcm, SampleFormat::S16_LE, 16000, 6, 1024, 4096};
	settings.maximum_frames = maximum_seconds * settings.request.rate;
	settings.tolerance_ns = tolerance_ns;
	settings.start_deadline = std::chrono::milliseconds(start_deadline_ms);
	settings.handoff_directory = handoff_dir;
	std::uint32_t seed = 1;
	BrokerPorts ports{
		[fake, &seed]() -> std::unique_ptr<CaptureSource> {
			if (fake)
				return std::make_unique<FakeCaptureSource>(FakeCaptureFaults{}, true, 1'000'000'000, seed++);
#if defined(BHP_MEDIA_HAS_ALSA)
			return std::make_unique<AlsaCaptureSource>();
#else
			FakeCaptureFaults refused; // Unreachable: main refuses to run without ALSA or --fake-source.
			refused.refuse_open = true;
			return std::make_unique<FakeCaptureSource>(refused);
#endif
		},
		[fake]() -> nlohmann::json {
			if (fake)
				return {{"model", "fake"}};
			auto found = find_xvf3800_usb_device();
			if (!found)
				return {{"error", found.error().code}};
			auto device = found.value();
			auto transfer = usbdevfs_control_transfer(device["usb_node"].get<std::string>());
			device["identity"] = transfer ? Xvf3800Control(transfer.value()).read_identity()
						      : nlohmann::json{{"error", transfer.error().code}};
			return device;
		},
		utc_now, random_id, [] { return std::chrono::steady_clock::now(); }};
	MediaBroker broker(settings, std::move(store.value()), ports);

	const int listener = listen_ui(ui_socket);
	if (listener < 0) {
		logger.log_error("media_broker_ui_socket_unavailable", trace, "MEDIA_BROKER.UI_SOCKET_UNAVAILABLE",
				 "Cannot listen on the UI socket", std::to_string(errno));
		return 1;
	}
	logger.log(bhp::StructuredLogLevel::info, "media_broker_started", trace,
		   {{"source", fake ? "fake" : "alsa"}, {"maximum_seconds", std::to_string(maximum_seconds)}});
	auto last_window_attempt = std::chrono::steady_clock::time_point{};
	bool window_logged = false;
	while (!stopping) {
		const auto now = std::chrono::steady_clock::now();
		if (!broker.window_attached() && now - last_window_attempt >= std::chrono::seconds(1)) {
			last_window_attempt = now;
			auto client = window::MediaWindowClient::connect(window_socket, static_cast<std::uint32_t>(supervisor_uid));
			if (client) {
				broker.attach_window(std::move(client.value()));
				logger.log(bhp::StructuredLogLevel::info, "media_window_connected", trace);
				window_logged = false;
			} else if (!window_logged) {
				logger.log(bhp::StructuredLogLevel::warn, "media_window_unavailable", trace,
					   {{"error_code", client.error().code}});
				window_logged = true;
			}
		}
		for (int attempt = 0; attempt < 2; ++attempt) {
			const int peer = ::accept4(listener, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
			if (peer < 0)
				break;
			const auto uid = window::peer_uid(peer);
			if (!uid || *uid != ui_uid) {
				::close(peer);
				logger.log(bhp::StructuredLogLevel::warn, "media_broker_ui_refused", trace, {{"reason", "peer_uid"}});
			} else if (!broker.attach_ui(std::make_unique<window::FrameChannel>(peer))) {
				logger.log(bhp::StructuredLogLevel::warn, "media_broker_ui_refused", trace, {{"reason", "already_attached"}});
			}
		}
		broker.step();
		pollfd descriptors[1] = {{listener, POLLIN, 0}};
		::poll(descriptors, 1, 10);
	}
	broker.abort("MEDIA_BROKER.STOPPING");
	::close(listener);
	::unlink(ui_socket.c_str());
	logger.log(bhp::StructuredLogLevel::info, "media_broker_stopped", trace);
	return 0;
}
