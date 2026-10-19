// bhp-media-capture-check: records one take through the broker's capture path and
// prints a JSON summary. A bring-up and evidence tool, not the broker: it takes no
// lease and runs only when started by hand.
#include "media_broker/alsa_capture_source.h"
#include "media_broker/take_store.h"
#include "media_broker/xvf3800_control.h"
#include <csignal>
#include <ctime>
#include <iostream>
#include <memory>
#include <string>
namespace
{
volatile std::sig_atomic_t stop_requested = 0;

void request_stop(int) { stop_requested = 1; }

const char *kUsage = "usage: bhp-media-capture-check --pcm NAME --seconds 1..600 --take-root DIR\n"
		     "  [--channels 6] [--rate 16000] [--period-frames 1024] [--buffer-frames 4096]\n"
		     "  [--tolerance-ns 4000000] [--no-device-control] [--fake]\n";

std::string utc_now()
{
	const std::time_t now = std::time(nullptr);
	std::tm parts{};
	gmtime_r(&now, &parts);
	char text[32];
	std::strftime(text, sizeof text, "%Y-%m-%dT%H:%M:%SZ", &parts);
	return text;
}

bool parse_unsigned(const std::string &text, unsigned long long maximum, unsigned long long &value)
{
	if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos || text.size() > 12)
		return false;
	value = std::stoull(text);
	return value > 0 && value <= maximum;
}

int fail(const media_broker::MediaError &error, nlohmann::json summary = nlohmann::json::object())
{
	summary["error"] = {{"code", error.code}, {"message", error.message}, {"context", error.context}};
	std::cout << summary.dump() << '\n';
	return 1;
}
} // namespace

int main(int argc, char **argv)
{
	using namespace media_broker;
	CaptureRequest request;
	std::string take_root;
	unsigned long long seconds = 0, tolerance_ns = 4'000'000;
	bool device_control = true, fake = false;
	for (int at = 1; at < argc; ++at) {
		const std::string option = argv[at];
		const bool has_value = at + 1 < argc;
		unsigned long long number = 0;
		if (option == "--no-device-control")
			device_control = false;
		else if (option == "--fake")
			fake = true;
		else if (option == "--pcm" && has_value)
			request.pcm = argv[++at];
		else if (option == "--take-root" && has_value)
			take_root = argv[++at];
		else if (option == "--seconds" && has_value && parse_unsigned(argv[++at], 600, number))
			seconds = number;
		else if (option == "--tolerance-ns" && has_value && parse_unsigned(argv[++at], 4'000'000, number))
			tolerance_ns = number;
		else if (option == "--channels" && has_value && parse_unsigned(argv[++at], 32, number))
			request.channels = static_cast<unsigned>(number);
		else if (option == "--rate" && has_value && parse_unsigned(argv[++at], 384000, number))
			request.rate = static_cast<unsigned>(number);
		else if (option == "--period-frames" && has_value && parse_unsigned(argv[++at], 16384, number))
			request.period_frames = static_cast<unsigned>(number);
		else if (option == "--buffer-frames" && has_value && parse_unsigned(argv[++at], 1048576, number))
			request.buffer_frames = static_cast<unsigned>(number);
		else {
			std::cerr << kUsage;
			return 2;
		}
	}
	if (request.pcm.empty() || take_root.empty() || seconds == 0) {
		std::cerr << kUsage;
		return 2;
	}
	std::signal(SIGINT, request_stop);
	std::signal(SIGTERM, request_stop);

	nlohmann::json device = nullptr;
	if (device_control && !fake) {
		auto found = find_xvf3800_usb_device();
		if (found) {
			device = found.value();
			auto transfer = usbdevfs_control_transfer(found.value()["usb_node"].get<std::string>());
			device["identity"] = transfer ? Xvf3800Control(transfer.value()).read_identity()
						      : nlohmann::json{{"error", transfer.error().code}};
		} else {
			device = {{"error", found.error().code}};
		}
	}
	const std::uint64_t maximum_frames = seconds * request.rate;
	const std::uint64_t planned_bytes = 44 + maximum_frames * request.channels * sample_width(request.format);
	auto store = TakeStore::open(take_root, {64, 8ULL << 30, 16ULL << 20});
	if (!store)
		return fail(store.error());
	if (auto admitted = store.value().admit(planned_bytes); !admitted)
		return fail(admitted.error());

	std::unique_ptr<CaptureSource> source;
	if (fake)
		source = std::make_unique<FakeCaptureSource>(FakeCaptureFaults{}, true);
	else
		source = std::make_unique<AlsaCaptureSource>();
	auto format = source->open(request);
	if (!format)
		return fail(format.error(), {{"device", device}});
	nlohmann::json summary = {{"pcm", format.value().pcm},
				  {"period_frames", format.value().period_frames},
				  {"buffer_frames", format.value().buffer_frames},
				  {"tstamp_type", to_string(format.value().timestamp_type)},
				  {"simulated", format.value().simulated},
				  {"device", device}};
	auto writer = TakeWriter::begin(store.value(), format.value(),
					{utc_now(), tolerance_ns, maximum_frames, device});
	if (!writer) {
		source->close();
		return fail(writer.error(), summary);
	}
	std::vector<std::uint8_t> samples;
	StopReason reason = StopReason::Requested;
	std::optional<MediaError> failure;
	while (!stop_requested) {
		if (writer.value()->frames() + format.value().period_frames > maximum_frames) {
			reason = StopReason::MaximumDuration;
			break;
		}
		auto period = source->read_period(samples);
		if (!period) {
			if (period.error().code == "MEDIA_CAPTURE.XRUN")
				writer.value()->count_xrun();
			else
				failure = period.error();
			break;
		}
		if (auto appended = writer.value()->append(samples, period.value()); !appended) {
			failure = appended.error();
			break;
		}
	}
	source->close();
	const auto &continuity = writer.value()->continuity();
	summary["frames"] = writer.value()->frames();
	summary["xruns"] = continuity.xruns();
	summary["periods"] = continuity.periods().size();
	summary["max_abs_deviation_ns"] = continuity.max_abs_deviation_ns()
						  ? nlohmann::json(*continuity.max_abs_deviation_ns())
						  : nlohmann::json(nullptr);
	if (failure) {
		writer.value()->abandon();
		return fail(*failure, summary);
	}
	auto finished = writer.value()->finish(reason);
	if (!finished)
		return fail(finished.error(), summary);
	summary["take_id"] = finished.value().take_id;
	summary["stop_reason"] = to_string(finished.value().stop_reason);
	summary["wav_bytes"] = finished.value().wav_bytes;
	std::cout << summary.dump() << '\n';
	return 0;
}
