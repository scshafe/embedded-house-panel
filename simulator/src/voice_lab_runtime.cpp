#include "simulator/voice_lab_runtime.hpp"
#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

namespace bhp::simulator
{
namespace
{
namespace lease = media_lease_protocol;
namespace window = media_window_protocol;
const std::string kSession(32, 'f');
const window::PresentationBinding kBinding{2, kSession, 1, "sha256:" + std::string(64, '0'), std::string(32, 'c'), 1};

std::string padded(const std::string &prefix, std::uint64_t value)
{
	auto digits = std::to_string(value);
	return prefix + std::string(digits.size() < 16 ? 16 - digits.size() : 0, '0') + digits;
}

std::pair<int, int> socket_pair()
{
	int pair[2];
	if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, pair) != 0)
		throw std::runtime_error("SIMULATOR.VOICE_LAB_UNAVAILABLE");
	return {pair[0], pair[1]};
}
} // namespace

std::unique_ptr<VoiceLabRuntime> VoiceLabRuntime::in_process(const std::uint64_t maximum_seconds)
{
	std::unique_ptr<VoiceLabRuntime> runtime(new VoiceLabRuntime());
	const char *base = std::getenv("TMPDIR");
	std::string pattern = std::string(base ? base : "/tmp") + "/bhp-sim-voice-lab-XXXXXX";
	if (::mkdtemp(pattern.data()) == nullptr)
		throw std::runtime_error("SIMULATOR.VOICE_LAB_UNAVAILABLE");
	runtime->temporary_root_ = pattern;
	runtime->take_root_ = pattern + "/takes";
	runtime->handoff_root_ = pattern + "/handoff";
	if (::mkdir(runtime->handoff_root_.c_str(), 0700) != 0)
		throw std::runtime_error("SIMULATOR.VOICE_LAB_UNAVAILABLE");
	auto store = media_broker::TakeStore::open(runtime->take_root_, {32, 1ULL << 31, 0});
	if (!store)
		throw std::runtime_error("SIMULATOR.VOICE_LAB_UNAVAILABLE");
	media_broker::BrokerSettings settings;
	settings.request = {"simulated", media_broker::SampleFormat::S16_LE, 16000, 6, 1024, 4096};
	settings.maximum_frames = maximum_seconds * settings.request.rate;
	settings.tolerance_ns = 1'000'000;
	settings.handoff_directory = runtime->handoff_root_;
	auto *self = runtime.get();
	media_broker::BrokerPorts ports{
		[self] {
			return std::make_unique<media_broker::FakeCaptureSource>(media_broker::FakeCaptureFaults{}, true,
										 1'000'000'000,
										 static_cast<std::uint32_t>(++self->counter_));
		},
		[] { return nlohmann::json{{"model", "simulated"}}; },
		[] {
			const std::time_t now = std::time(nullptr);
			std::tm parts{};
			gmtime_r(&now, &parts);
			char text[32];
			std::strftime(text, sizeof text, "%Y-%m-%dT%H:%M:%SZ", &parts);
			return std::string(text);
		},
		[self] { return padded("simulated-lease-", ++self->counter_); },
		[] { return std::chrono::steady_clock::now(); }};
	runtime->broker_ = std::make_unique<media_broker::MediaBroker>(settings, std::move(store.value()), ports);
	const auto [supervisor_side, broker_window_side] = socket_pair();
	runtime->supervisor_ = std::make_unique<window::FrameChannel>(supervisor_side);
	auto window_client = window::MediaWindowClient::for_connected_peer(broker_window_side, ::getuid());
	const auto [ui_side, broker_ui_side] = socket_pair();
	auto client = lease::MediaLeaseClient::for_connected_peer(ui_side, ::getuid());
	if (!window_client || !client)
		throw std::runtime_error("SIMULATOR.VOICE_LAB_UNAVAILABLE");
	runtime->broker_->attach_window(std::move(window_client.value()));
	runtime->broker_->attach_ui(std::make_unique<window::FrameChannel>(broker_ui_side));
	runtime->client_ = std::move(client.value());
	runtime->set_window(false);
	return runtime;
}

std::unique_ptr<VoiceLabRuntime> VoiceLabRuntime::external(const std::string &socket_path, const std::uint32_t broker_uid)
{
	auto client = lease::MediaLeaseClient::connect(socket_path, broker_uid);
	if (!client)
		throw std::runtime_error("SIMULATOR.VOICE_LAB_BROKER_UNAVAILABLE:" + client.error().code);
	std::unique_ptr<VoiceLabRuntime> runtime(new VoiceLabRuntime());
	runtime->client_ = std::move(client.value());
	return runtime;
}

VoiceLabRuntime::~VoiceLabRuntime()
{
	broker_.reset();
	if (!temporary_root_.empty()) {
		std::error_code ignored;
		std::filesystem::remove_all(temporary_root_, ignored);
	}
}

void VoiceLabRuntime::set_window(const bool open)
{
	if (!supervisor_ || (window_sent_ && open == window_open_))
		return;
	window_open_ = open;
	window_sent_ = true;
	auto frame = window::encode(window::WindowMessage{kSession, ++supervisor_sequence_, open,
							  open ? window::WindowReason::DayPresent : window::WindowReason::Absence});
	if (frame)
		(void)supervisor_->send(frame.value());
}

bool VoiceLabRuntime::start_take(const std::string &operation_ref, const std::optional<TouchEvidence> &evidence)
{
	if (busy() || !client_->connected())
		return false;
	std::string touch;
	window::PresentationBinding binding = kBinding;
	if (broker_) {
		// A synthetic touch, granted once by the stand-in supervisor.
		touch = padded("simulated-touch-", ++counter_);
		granted_touches_.push_back(touch);
	} else {
		if (!evidence)
			return false;
		touch = evidence->touch_observation_id;
		binding = evidence->presentation_binding;
	}
	request_id_ = padded("ui-request-", ++counter_) + std::to_string(::getpid());
	if (!client_->send(lease::ArmRequest{request_id_, lease::Purpose::VoiceTake, touch, binding, operation_ref}))
		return false;
	pending_ = Pending::Arming;
	return true;
}

void VoiceLabRuntime::stop()
{
	if (pending_ == Pending::Recording)
		(void)client_->send(lease::StopRequest{lease_id_});
}

void VoiceLabRuntime::discard(const std::string &take_id)
{
	if (!take_id.empty())
		(void)client_->send(lease::DiscardRequest{take_id});
}

void VoiceLabRuntime::hand_off(const std::string &take_id)
{
	if (!take_id.empty())
		(void)client_->send(lease::HandOffRequest{take_id});
}

void VoiceLabRuntime::abort()
{
	if (broker_)
		broker_->abort("MEDIA_BROKER.ABORTED");
	else if (pending_ == Pending::Recording)
		(void)client_->send(lease::StopRequest{lease_id_});
}

void VoiceLabRuntime::pump_supervisor()
{
	if (!supervisor_)
		return;
	for (int frames = 0; frames < 4; ++frames) {
		auto received = supervisor_->receive();
		if (!received || !received.value())
			return;
		auto claim = window::decode_claim_request(*received.value());
		if (!claim)
			return;
		window::ClaimResult result{kSession, ++supervisor_sequence_, claim.value().request_id, std::nullopt, {}};
		auto granted = std::find(granted_touches_.begin(), granted_touches_.end(), claim.value().touch_observation_id);
		if (!window_open_)
			result.error_code = "window_closed";
		else if (granted == granted_touches_.end())
			result.error_code = "observation_unknown";
		else {
			granted_touches_.erase(granted);
			result.envelope = window::ClaimEnvelope{claim.value().touch_observation_id, counter_,
								claim.value().canonical_operation_digest,
								claim.value().presentation_binding, 120};
		}
		auto frame = window::encode(result);
		if (frame)
			(void)supervisor_->send(frame.value());
	}
}

std::vector<VoiceLabRuntime::Event> VoiceLabRuntime::pump()
{
	std::vector<Event> events;
	if (broker_) {
		pump_supervisor();
		broker_->step();
		pump_supervisor();
		broker_->step();
	}
	auto received = client_->poll();
	if (!received) {
		// A broker that went away ends any take in progress.
		if (pending_ != Pending::None) {
			pending_ = Pending::None;
			events.push_back({Event::Kind::Failed, {}, 0, {}, 0, "MEDIA_LEASE.BROKER_UNAVAILABLE"});
		}
		return events;
	}
	for (const auto &event : received.value()) {
		if (const auto *armed = std::get_if<lease::Armed>(&event)) {
			if (pending_ != Pending::Arming || armed->request_id != request_id_)
				continue;
			lease_id_ = armed->lease_id;
			pending_ = Pending::Starting;
			(void)client_->send(lease::StartRequest{lease_id_});
		} else if (std::get_if<lease::Started>(&event)) {
			pending_ = Pending::Recording;
			events.push_back({Event::Kind::Started, {}, 0, {}, 0, {}});
		} else if (const auto *level = std::get_if<lease::Level>(&event)) {
			events.push_back({Event::Kind::Level,
					  std::vector<int>(level->peak_decibels_tenths.begin(), level->peak_decibels_tenths.end()),
					  level->elapsed_milliseconds, {}, 0, {}});
		} else if (const auto *ended = std::get_if<lease::Ended>(&event)) {
			pending_ = Pending::None;
			if (ended->outcome == lease::Outcome::Committed)
				events.push_back({Event::Kind::Committed, {}, ended->frames * 1000 / 16000, ended->take_id,
						  ended->frames, {}});
			else
				events.push_back({Event::Kind::Failed, {}, 0, {}, 0, ended->error_code});
		} else if (const auto *refused = std::get_if<lease::Refused>(&event)) {
			const bool ours = refused->request_id == request_id_ || refused->request_id == lease_id_;
			if (ours && pending_ != Pending::None && pending_ != Pending::Recording) {
				pending_ = Pending::None;
				events.push_back({Event::Kind::Failed, {}, 0, {}, 0, refused->error_code});
			}
		} else if (const auto *discarded = std::get_if<lease::Discarded>(&event)) {
			events.push_back({Event::Kind::Discarded, {}, 0, discarded->take_id, 0, {}});
		} else if (const auto *handed_off = std::get_if<lease::HandedOff>(&event)) {
			events.push_back({Event::Kind::HandedOff, {}, 0, handed_off->take_id, 0, {}});
		}
	}
	return events;
}
} // namespace bhp::simulator
