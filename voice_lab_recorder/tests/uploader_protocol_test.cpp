// The uploader protocol (wire plan Decision 4): round trips, closed key sets,
// bounds, and the client's exact-UID check over a socket pair.
#include "voice_lab_recorder/uploader_protocol.h"
#include <iostream>
#include <sys/socket.h>
#include <unistd.h>

using namespace voice_lab_recorder::uploader_protocol;
using voice_lab_recorder::Result;

namespace
{
int failures = 0;

void expect(const bool condition, const std::string &what)
{
	if (!condition) {
		++failures;
		std::cerr << "FAILED: " << what << '\n';
	}
}

const std::string kTake = "take-" + std::string(64, 'a');

State sample()
{
	State state;
	state.sequence = 7;
	state.sign_in = {"pending", "https://id.example/device", "ABCD-EFGH", 900};
	state.approval = {"preview", "voice-lab.example.ts.net", "nEXAMPLE1", "2027-10-19T12:00:00Z", std::string(64, 'b')};
	state.queued = 1;
	state.kept = 2;
	state.blocked = "VOICE_LAB.APPROVAL_MISSING";
	state.catalog_version = 3;
	state.prompts = {{"reference-neutral-01", "A quiet morning – é", "reference", "missing"},
			 {"room-tone-01", "", "room-tone", "kept"}};
	return state;
}

void requests()
{
	for (const UiRequest &request : {UiRequest(StatusRequest{}), UiRequest(SignInRequest{}),
					 UiRequest(ApproveRequest{std::string(64, 'c')}),
					 UiRequest(KeepRequest{kTake, "reference-neutral-01", "2026-10-19T12:00:00Z"})}) {
		auto frame = encode(request);
		auto decoded = frame.has_value() ? decode_ui_request(frame.value()) : Result<UiRequest>::failure({});
		expect(decoded.has_value() && decoded.value().index() == request.index(), "request round trip");
	}
	expect(!encode(UiRequest(KeepRequest{"take-1", "p", "2026-10-19T12:00:00Z"})).has_value(), "keep names a take id");
	expect(!encode(UiRequest(KeepRequest{kTake, "Not An Id", "2026-10-19T12:00:00Z"})).has_value(),
	       "keep names a stable prompt id");
	expect(!encode(UiRequest(ApproveRequest{"short"})).has_value(), "approve names a digest");
	expect(!decode_ui_request("{\"type\":\"status\",\"version\":1,\"extra\":true}\n").has_value(), "closed key set");
	expect(!decode_ui_request("{\"type\":\"status\",\"version\":2}\n").has_value(), "version 1 only");
	expect(!decode_ui_request("{\"type\":\"status\",\"version\":1}").has_value(), "a frame ends with a newline");
	expect(!decode_ui_request("{\"type\":\"refresh_token\",\"version\":1}\n").has_value(), "unknown requests refused");
}

void events()
{
	const State state = sample();
	auto frame = encode(UploaderEvent(state));
	auto decoded = frame.has_value() ? decode_uploader_event(frame.value()) : Result<UploaderEvent>::failure({});
	expect(decoded.has_value() && encode(decoded.value()).value() == frame.value(), "state round trip");
	if (decoded.has_value()) {
		const auto &got = std::get<State>(decoded.value());
		expect(got.sign_in.user_code == "ABCD-EFGH" && got.prompts.size() == 2 && got.catalog_version == 3u &&
			       got.prompts[0].text == "A quiet morning – é",
		       "state carries the code and the prompts");
	}
	State signed_in;
	signed_in.sign_in.state = "signed_in";
	auto quiet = encode(UploaderEvent(signed_in));
	expect(quiet.has_value() && quiet.value().find("\"user_code\":null") != std::string::npos,
	       "signed in, no code is sent");
	State leaking = sample();
	leaking.sign_in.state = "signed_in";
	expect(!encode(UploaderEvent(leaking)).has_value(), "a code outside a pending sign-in is refused");
	State plain_http = sample();
	plain_http.sign_in.verification_uri = "http://id.example/device";
	expect(!encode(UploaderEvent(plain_http)).has_value(), "the verification URI is https");
	State many = sample();
	many.prompts.assign(kMaximumPrompts + 1, many.prompts[0]);
	expect(!encode(UploaderEvent(many)).has_value(), "at most 200 prompts");
	State large = sample();
	large.prompts.assign(kMaximumPrompts, {"reference-neutral-01", std::string(2000, 'x'), "reference", "missing"});
	expect(!encode(UploaderEvent(large)).has_value(), "a state over 64 KiB is refused, not sent cut");
	auto refused = encode(UploaderEvent(Refused{"keep", "VOICE_LAB.PROMPT_NOT_IN_CATALOG"}));
	expect(refused.has_value() && decode_uploader_event(refused.value()).has_value(), "refused round trip");
	expect(!encode(UploaderEvent(Refused{"export", "VOICE_LAB.X"})).has_value(), "refused names a request");
}

void client()
{
	int pair[2];
	::socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, pair);
	auto rejected = UploaderClient::for_connected_peer(pair[0], ::getuid() + 1);
	expect(!rejected.has_value() && rejected.error().code == "VOICE_LAB.UPLOADER_PEER_REJECTED",
	       "another UID is refused");
	::close(pair[1]);
	::socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, pair);
	auto connected = UploaderClient::for_connected_peer(pair[0], ::getuid());
	expect(connected.has_value(), "the uploader UID is accepted");
	if (!connected.has_value())
		return;
	auto &uploader = *connected.value();
	expect(uploader.send(StatusRequest{}).has_value(), "a request is sent");
	char buffer[256];
	const auto got = ::read(pair[1], buffer, sizeof buffer);
	expect(got > 0 && decode_ui_request(std::string(buffer, static_cast<std::size_t>(got))).has_value(),
	       "the uploader reads a valid frame");
	const auto frame = encode(UploaderEvent(sample())).value();
	expect(::write(pair[1], frame.data(), frame.size()) == static_cast<ssize_t>(frame.size()), "state written");
	auto events = uploader.poll();
	expect(events.has_value() && events.value().size() == 1, "the client reads the state");
	const std::string bad = "{\"type\":\"state\",\"version\":1}\n";
	(void)::write(pair[1], bad.data(), bad.size());
	expect(!uploader.poll().has_value() && !uploader.connected(), "a malformed event closes the client");
	::close(pair[1]);
}
} // namespace

int main()
{
	requests();
	events();
	client();
	if (failures)
		std::cerr << failures << " failure(s)\n";
	return failures ? 1 : 0;
}
