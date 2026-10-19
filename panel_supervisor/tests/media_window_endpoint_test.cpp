#include "panel_supervisor/media_window_endpoint.hpp"
#include <iostream>
#include <set>
#include <vector>
#include <sys/socket.h>
#include <unistd.h>
using namespace house_panel::supervisor;
namespace wire = bhp::media_window_protocol;
namespace {
int failures = 0;
void expect(bool value, const std::string &label) {
	if (!value) { ++failures; std::cerr << "FAIL: " << label << '\n'; }
}
const std::string kSession(32, 'a');
const wire::PresentationBinding kBinding{2, kSession, 7, "sha256:" + std::string(64, 'c'), std::string(32, 'd'), 3};
wire::ClaimRequest claim(const std::string &observation) {
	return {"request-" + observation, observation, "sha256:" + std::string(64, 'e'), kBinding};
}

/** A redeem port that consumes each observation once, like the authority. */
struct FakeAuthority {
	std::set<std::string> consumed;
	int calls{0};
	Result<TouchProvenanceEnvelope> operator()(const TouchObservationClaim &request) {
		++calls;
		if (request.touch_observation_id.find("unknown") != std::string::npos || !consumed.insert(request.touch_observation_id).second)
			return Result<TouchProvenanceEnvelope>::failure(SupervisorError(ErrorCode::InvalidPolicyObservation,
				"refused", {{"reason", consumed.count(request.touch_observation_id) ? "observation_already_consumed" : "observation_unknown"}}));
		return Result<TouchProvenanceEnvelope>::success({1, request.touch_observation_id, 11,
			TouchCapabilityHolderAudience::MediaBroker, request.canonical_operation_digest,
			request.expected_presentation_binding, {}, std::chrono::milliseconds(140), {}, std::nullopt, true});
	}
};

bool pump_until(wire::MediaWindowClient &client, MediaWindowEndpoint &endpoint, FakeAuthority &authority,
	std::vector<wire::ClaimResult> &results) {
	for (int round = 0; round < 50; ++round) {
		auto pumped = endpoint.pump([&](const TouchObservationClaim &c) { return authority(c); });
		auto update = client.poll();
		if (!update) return false;
		for (auto &result : update.value().claim_results) results.push_back(result);
		if (!results.empty()) return true;
		::usleep(1000);
	}
	return true;
}
}

int main() {
	int pair[2];
	::socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, pair);
	expect(!MediaWindowEndpoint::for_connected_peer(::dup(pair[0]), ::getuid() + 1, kSession),
		"a peer that is not the broker UID is refused");
	auto endpoint = MediaWindowEndpoint::for_connected_peer(pair[0], ::getuid(), kSession);
	auto client = wire::MediaWindowClient::for_connected_peer(pair[1], ::getuid());
	expect(endpoint && client, "endpoint and client connect");
	auto &supervisor = *endpoint.value();
	auto &broker = *client.value();
	FakeAuthority authority;
	std::vector<wire::ClaimResult> results;

	auto first = broker.poll();
	expect(first && !broker.window_open() && broker.last_reason() == wire::WindowReason::Absence,
		"the current (closed) window arrives on connect");
	supervisor.set_window(true, wire::WindowReason::DayPresent);
	expect(broker.poll() && broker.window_open(), "the window opens");
	supervisor.set_window(true, wire::WindowReason::DayPresent);
	expect(broker.poll() && broker.window_open(), "an unchanged window sends nothing and stays open");

	expect(broker.claim(claim("observation-00000000000001")).has_value(), "claim sent");
	expect(pump_until(broker, supervisor, authority, results) && results.size() == 1 && results[0].envelope
		&& results[0].envelope->observation_sequence == 11 && results[0].envelope->contact_duration_milliseconds == 140,
		"an open window consumes the observation and returns its envelope");
	results.clear();
	expect(broker.claim(claim("observation-00000000000001")).has_value(), "the same observation again");
	expect(pump_until(broker, supervisor, authority, results) && results.size() == 1 && !results[0].envelope
		&& results[0].error_code == "observation_already_consumed", "a replay is refused by the authority");
	results.clear();

	// Close-first: the broker sees the window close before any further claim.
	supervisor.set_window(false, wire::WindowReason::SuspendPreparation);
	auto closed = broker.poll();
	expect(closed && closed.value().window_changed && !broker.window_open()
		&& broker.last_reason() == wire::WindowReason::SuspendPreparation, "suspend preparation closes the window");
	const int calls_before = authority.calls;
	const auto frame = wire::encode(claim("observation-00000000000002")).value();
	expect(::write(pair[1], frame.data(), frame.size()) == static_cast<ssize_t>(frame.size()), "raw claim while closed");
	expect(pump_until(broker, supervisor, authority, results) && results.size() == 1
		&& results[0].error_code == "window_closed" && authority.calls == calls_before,
		"a closed window refuses without consuming");

	expect(::write(pair[1], "{}\n", 3) == 3, "malformed frame");
	for (int round = 0; round < 20 && supervisor.connected(); ++round) {
		(void)supervisor.pump([&](const TouchObservationClaim &c) { return authority(c); });
		::usleep(1000);
	}
	expect(!supervisor.connected(), "a malformed claim drops the broker");
	expect(!MediaWindowEndpoint::listen("/tmp/media-window-test.sock", 1103, kSession),
		"listen refuses a non-root caller or an untrusted parent");
	std::cout << "media_window_endpoint: " << failures << " failures\n";
	return failures ? 1 : 0;
}
