// The broker end to end on one host: a UI over a socketpair, the real supervisor
// MediaWindowEndpoint over another with a fake touch authority, and the paced
// fake capture source. Covers every way a voice_take lease can end.
#include "media_broker/media_broker.h"
#include "panel_supervisor/media_window_endpoint.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
using namespace media_broker;
namespace lease = bhp::media_lease_protocol;
namespace wire = bhp::media_window_protocol;
namespace supervisor = house_panel::supervisor;
namespace
{
int failures = 0;
void expect(bool value, const std::string &label)
{
	if (!value) {
		++failures;
		std::cerr << "FAIL: " << label << '\n';
	}
}

const std::string kSession(32, 'a');
const wire::PresentationBinding kBinding{2, kSession, 7, "sha256:" + std::string(64, 'c'), std::string(32, 'd'), 3};

/** The supervisor's authority as the endpoint sees it: each known observation once. */
struct FakeAuthority {
	std::map<std::string, bool> observations;
	std::vector<std::string> digests;
	supervisor::Result<supervisor::TouchProvenanceEnvelope> operator()(const supervisor::TouchObservationClaim &claim)
	{
		digests.push_back(claim.canonical_operation_digest);
		auto found = observations.find(claim.touch_observation_id);
		if (found == observations.end() || found->second)
			return supervisor::Result<supervisor::TouchProvenanceEnvelope>::failure(supervisor::SupervisorError(
				supervisor::ErrorCode::InvalidPolicyObservation, "refused",
				{{"reason", found == observations.end() ? "observation_unknown" : "observation_already_consumed"}}));
		found->second = true;
		return supervisor::Result<supervisor::TouchProvenanceEnvelope>::success(
			{1, claim.touch_observation_id, 5, supervisor::TouchCapabilityHolderAudience::MediaBroker,
			 claim.canonical_operation_digest, claim.expected_presentation_binding, {}, std::chrono::milliseconds(90),
			 {}, std::nullopt, true});
	}
};

struct Harness {
	std::string root;
	FakeAuthority authority;
	FakeCaptureFaults faults;
	std::unique_ptr<supervisor::MediaWindowEndpoint> endpoint;
	std::unique_ptr<MediaBroker> broker;
	int ui{-1};
	std::string buffered;
	int counter{0};

	explicit Harness(std::uint64_t maximum_frames = 16000 * 600, std::chrono::milliseconds start_deadline = std::chrono::milliseconds(1000))
	{
		const char *base = std::getenv("TMPDIR");
		root = std::string(base ? base : "/tmp") + "/media-broker-test-XXXXXX";
		if (mkdtemp(root.data()) == nullptr)
			std::abort();
		int window_pair[2], ui_pair[2];
		::socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, window_pair);
		::socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, ui_pair);
		endpoint = std::move(supervisor::MediaWindowEndpoint::for_connected_peer(window_pair[0], ::getuid(), kSession).value());
		BrokerSettings settings;
		settings.request = {"fake", SampleFormat::S16_LE, 16000, 6, 1024, 4096};
		settings.maximum_frames = maximum_frames;
		settings.tolerance_ns = 1'000'000;
		settings.start_deadline = start_deadline;
		BrokerPorts ports{[this] { return std::make_unique<FakeCaptureSource>(faults, true, 1'000'000'000, ++counter); },
				  [] { return nlohmann::json{{"model", "fixture"}}; },
				  [] { return std::string("2026-10-19T12:00:00Z"); },
				  [this] { return "lease-" + std::to_string(1000000000000000000ULL + ++counter); },
				  [] { return std::chrono::steady_clock::now(); }};
		broker = std::make_unique<MediaBroker>(settings, TakeStore::open(root + "/takes", {8, 1ULL << 32, 0}).value(), ports);
		broker->attach_window(std::move(wire::MediaWindowClient::for_connected_peer(window_pair[1], ::getuid()).value()));
		broker->attach_ui(std::make_unique<wire::FrameChannel>(ui_pair[1]));
		ui = ui_pair[0];
		cycle();
	}
	~Harness()
	{
		broker.reset();
		if (ui >= 0)
			::close(ui);
		std::filesystem::remove_all(root);
	}
	void cycle()
	{
		(void)endpoint->pump([this](const supervisor::TouchObservationClaim &claim) { return authority(claim); });
		broker->step();
	}
	void send(const lease::UiRequest &request)
	{
		const auto frame = lease::encode(request).value();
		if (::write(ui, frame.data(), frame.size()) != static_cast<ssize_t>(frame.size()))
			std::abort();
	}
	/** Runs the loop until an event of type T arrives (or the timeout), collecting levels. */
	template <typename T> std::optional<T> await(std::chrono::milliseconds timeout = std::chrono::milliseconds(3000), int *levels = nullptr)
	{
		const auto until = std::chrono::steady_clock::now() + timeout;
		while (std::chrono::steady_clock::now() < until) {
			cycle();
			char bytes[4096];
			const auto got = ::recv(ui, bytes, sizeof bytes, MSG_DONTWAIT);
			if (got > 0)
				buffered.append(bytes, static_cast<std::size_t>(got));
			for (auto end = buffered.find('\n'); end != std::string::npos; end = buffered.find('\n')) {
				auto event = lease::decode_broker_event(buffered.substr(0, end + 1));
				buffered.erase(0, end + 1);
				if (!event)
					return std::nullopt;
				if (levels && std::holds_alternative<lease::Level>(event.value()))
					++*levels;
				if (const auto *wanted = std::get_if<T>(&event.value()))
					return *wanted;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}
		return std::nullopt;
	}
	std::string arm(const std::string &observation, lease::Purpose purpose = lease::Purpose::VoiceTake)
	{
		const std::string request = "request-" + std::to_string(1000000000000000000ULL + ++counter);
		send(lease::ArmRequest{request, purpose, observation, kBinding, "prompt:0001"});
		return request;
	}
	std::size_t takes() const
	{
		std::size_t count = 0;
		for (const auto &entry : std::filesystem::directory_iterator(root + "/takes"))
			count += entry.path().filename().string().rfind("take-", 0) == 0;
		return count;
	}
	std::size_t partials() const
	{
		std::size_t count = 0;
		for (const auto &entry : std::filesystem::directory_iterator(root + "/takes"))
			count += entry.path().filename().string().rfind(".incoming-", 0) == 0;
		return count;
	}
};

const std::string kObservation = "observation-00000000000001";

void test_window_and_purpose_gates()
{
	Harness h;
	const auto closed = h.arm(kObservation);
	auto refused = h.await<lease::Refused>();
	expect(refused && refused->request_id == closed && refused->error_code == "MEDIA_BROKER.WINDOW_CLOSED",
	       "a closed window refuses arm");
	h.endpoint->set_window(true, wire::WindowReason::DayPresent);
	h.arm(kObservation, lease::Purpose::WishRecording);
	refused = h.await<lease::Refused>();
	expect(refused && refused->error_code == "MEDIA_BROKER.PURPOSE_DISABLED", "only voice_take is served");
	h.arm("observation-unknown-0000001");
	refused = h.await<lease::Refused>();
	expect(refused && refused->error_code == "MEDIA_BROKER.CLAIM_REFUSED.observation_unknown",
	       "an unknown observation is refused by the supervisor");
	expect(h.broker->state() == MediaBroker::State::Idle && h.takes() == 0, "nothing armed, nothing recorded");
}

void test_requested_take_commits()
{
	Harness h;
	h.authority.observations[kObservation] = false;
	h.endpoint->set_window(true, wire::WindowReason::DayPresent);
	const auto request = h.arm(kObservation);
	auto armed = h.await<lease::Armed>();
	expect(armed && armed->request_id == request, "armed");
	expect(h.authority.digests.size() == 1 &&
		       h.authority.digests[0] == voice_take_operation_digest(request, "prompt:0001"),
	       "the claim names the digest of this request's operation");
	h.send(lease::StartRequest{armed->lease_id});
	auto started = h.await<lease::Started>();
	expect(started && started->lease_id == armed->lease_id, "started");
	h.send(lease::StartRequest{armed->lease_id});
	auto again = h.await<lease::Refused>();
	expect(again && again->error_code == "MEDIA_BROKER.START_ALREADY_CONSUMED", "start is consumed once");
	h.arm("observation-00000000000002");
	auto busy = h.await<lease::Refused>();
	expect(busy && busy->error_code == "MEDIA_BROKER.BUSY", "one lease at a time");
	int levels = 0;
	(void)h.await<lease::Ended>(std::chrono::milliseconds(450), &levels);
	expect(levels >= 3, "level events about every 100 ms");
	h.send(lease::StopRequest{armed->lease_id});
	auto ended = h.await<lease::Ended>();
	expect(ended && ended->outcome == lease::Outcome::Committed && ended->stop_reason == "requested" &&
		       ended->frames > 0 && h.takes() == 1,
	       "a requested stop commits the take");
	if (ended && ended->outcome == lease::Outcome::Committed) {
		std::ifstream record(h.root + "/takes/" + ended->take_id + "/capture.json");
		const auto json = nlohmann::json::parse(record);
		expect(json["device"]["model"] == "fixture" && json["frames"] == ended->frames && json["xruns"] == 0,
		       "the record carries the device read at start");
	}
	if (ended && ended->outcome == lease::Outcome::Committed) {
		h.send(lease::DiscardRequest{ended->take_id});
		auto discarded = h.await<lease::Discarded>();
		expect(discarded && discarded->take_id == ended->take_id && h.takes() == 0, "a rejected take is discarded");
		h.send(lease::DiscardRequest{ended->take_id});
		auto again = h.await<lease::Refused>();
		expect(again && again->error_code == "MEDIA_TAKE.UNKNOWN", "a discarded take is gone");
	}
	h.send(lease::StopRequest{armed->lease_id});
	auto unknown = h.await<lease::Refused>();
	expect(unknown && unknown->error_code == "MEDIA_BROKER.LEASE_UNKNOWN", "a closed lease is terminal");
	h.arm(kObservation);
	auto replay = h.await<lease::Refused>();
	expect(replay && replay->error_code == "MEDIA_BROKER.CLAIM_REFUSED.observation_already_consumed",
	       "a touch is consumed once");
}

void test_maximum_duration()
{
	Harness h(8 * 1024);
	h.authority.observations[kObservation] = false;
	h.endpoint->set_window(true, wire::WindowReason::DayPresent);
	h.arm(kObservation);
	auto armed = h.await<lease::Armed>();
	h.send(lease::StartRequest{armed->lease_id});
	auto ended = h.await<lease::Ended>();
	expect(ended && ended->outcome == lease::Outcome::Committed && ended->stop_reason == "maximum-duration" &&
		       ended->frames == 8 * 1024,
	       "the take stops itself at the maximum");
}

void test_start_deadline()
{
	Harness h(16000 * 600, std::chrono::milliseconds(60));
	h.authority.observations[kObservation] = false;
	h.endpoint->set_window(true, wire::WindowReason::DayPresent);
	h.arm(kObservation);
	auto armed = h.await<lease::Armed>();
	auto expired = h.await<lease::Refused>(std::chrono::milliseconds(500));
	expect(armed && expired && expired->request_id == armed->lease_id &&
		       expired->error_code == "MEDIA_BROKER.LEASE_EXPIRED",
	       "an unstarted lease expires");
	h.send(lease::StartRequest{armed->lease_id});
	auto late = h.await<lease::Refused>();
	expect(late && late->error_code == "MEDIA_BROKER.LEASE_UNKNOWN", "an expired lease cannot start");
}

void test_failures_leave_nothing()
{
	{
		Harness h;
		h.authority.observations[kObservation] = false;
		h.endpoint->set_window(true, wire::WindowReason::DayPresent);
		h.arm(kObservation);
		auto armed = h.await<lease::Armed>();
		h.send(lease::StartRequest{armed->lease_id});
		(void)h.await<lease::Started>();
		(void)h.await<lease::Ended>(std::chrono::milliseconds(200));
		h.endpoint->set_window(false, wire::WindowReason::Absence);
		auto ended = h.await<lease::Ended>();
		expect(ended && ended->outcome == lease::Outcome::Failed && ended->error_code == "MEDIA_BROKER.WINDOW_CLOSED" &&
			       h.takes() == 0 && h.partials() == 0,
		       "a closing window fails the take and erases it");
	}
	{
		Harness h;
		h.faults.xrun_at_period = 3;
		h.authority.observations[kObservation] = false;
		h.endpoint->set_window(true, wire::WindowReason::DayPresent);
		h.arm(kObservation);
		auto armed = h.await<lease::Armed>();
		h.send(lease::StartRequest{armed->lease_id});
		auto ended = h.await<lease::Ended>();
		expect(ended && ended->outcome == lease::Outcome::Failed && ended->error_code == "MEDIA_CAPTURE.XRUN" &&
			       h.takes() == 0 && h.partials() == 0,
		       "an xrun fails the take");
	}
	{
		Harness h;
		h.authority.observations[kObservation] = false;
		h.endpoint->set_window(true, wire::WindowReason::DayPresent);
		h.arm(kObservation);
		auto armed = h.await<lease::Armed>();
		h.send(lease::StartRequest{armed->lease_id});
		(void)h.await<lease::Started>();
		::close(h.ui);
		h.ui = -1;
		for (int round = 0; round < 200 && h.broker->state() != MediaBroker::State::Idle; ++round) {
			h.cycle();
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}
		expect(h.broker->state() == MediaBroker::State::Idle && !h.broker->ui_attached() && h.takes() == 0 &&
			       h.partials() == 0,
		       "a UI that leaves ends the take and erases it");
	}
	{
		Harness h;
		h.authority.observations[kObservation] = false;
		h.endpoint->set_window(true, wire::WindowReason::DayPresent);
		h.arm(kObservation);
		auto armed = h.await<lease::Armed>();
		h.send(lease::StartRequest{armed->lease_id});
		(void)h.await<lease::Started>();
		h.broker->abort("MEDIA_BROKER.STOPPING");
		auto ended = h.await<lease::Ended>();
		expect(ended && ended->error_code == "MEDIA_BROKER.STOPPING" && h.takes() == 0 && h.partials() == 0,
		       "stopping the daemon ends the take");
	}
	{
		Harness h;
		h.authority.observations[kObservation] = false;
		h.endpoint->set_window(true, wire::WindowReason::DayPresent);
		const std::string junk = "{\"type\":\"arm\"}\n";
		expect(::write(h.ui, junk.data(), junk.size()) == static_cast<ssize_t>(junk.size()), "junk written");
		for (int round = 0; round < 50 && h.broker->ui_attached(); ++round)
			h.cycle();
		expect(!h.broker->ui_attached(), "a malformed request drops the UI");
	}
}
/** The UI's client library against the real broker: one committed take. */
void test_lease_client()
{
	Harness h;
	h.authority.observations[kObservation] = false;
	h.endpoint->set_window(true, wire::WindowReason::DayPresent);
	expect(!lease::MediaLeaseClient::for_connected_peer(::dup(h.ui), ::getuid() + 1),
	       "the client refuses a peer that is not the broker UID");
	auto client = lease::MediaLeaseClient::for_connected_peer(h.ui, ::getuid());
	h.ui = -1;
	expect(client.has_value(), "client connects");
	auto &ui = *client.value();
	auto next = [&](auto predicate) -> std::optional<lease::BrokerEvent> {
		for (int round = 0; round < 1500; ++round) {
			h.cycle();
			auto events = ui.poll();
			if (!events)
				return std::nullopt;
			for (auto &event : events.value())
				if (predicate(event))
					return event;
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}
		return std::nullopt;
	};
	const std::string request = "request-client-00000000001";
	expect(ui.send(lease::ArmRequest{request, lease::Purpose::VoiceTake, kObservation, kBinding, "prompt:0002"})
		       .has_value(),
	       "arm sent");
	auto armed = next([](const lease::BrokerEvent &e) { return std::holds_alternative<lease::Armed>(e); });
	expect(armed.has_value(), "armed through the client");
	const auto lease_id = std::get<lease::Armed>(*armed).lease_id;
	(void)ui.send(lease::StartRequest{lease_id});
	auto level = next([](const lease::BrokerEvent &e) { return std::holds_alternative<lease::Level>(e); });
	expect(level && std::get<lease::Level>(*level).peak_decibels_tenths.size() == 6, "six channel levels");
	(void)ui.send(lease::StopRequest{lease_id});
	auto ended = next([](const lease::BrokerEvent &e) { return std::holds_alternative<lease::Ended>(e); });
	expect(ended && std::get<lease::Ended>(*ended).outcome == lease::Outcome::Committed && h.takes() == 1,
	       "the client's take commits");
}
} // namespace

int main()
{
	test_window_and_purpose_gates();
	test_requested_take_commits();
	test_maximum_duration();
	test_start_deadline();
	test_failures_leave_nothing();
	test_lease_client();
	std::cout << "media_broker: " << failures << " failures\n";
	return failures ? 1 : 0;
}
