#include "bhp/media_window_protocol/media_window_protocol.h"
#include <iostream>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
using namespace bhp::media_window_protocol;
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
const PresentationBinding kBinding{2, std::string(32, 'b'), 7, "sha256:" + std::string(64, 'c'), std::string(32, 'd'), 3};
const ClaimRequest kClaim{"request-0000000000000001", "observation-00000000000001", "sha256:" + std::string(64, 'e'), kBinding};

void test_codec_round_trips()
{
	auto window = encode(WindowMessage{kSession, 1, true, WindowReason::DayPresent});
	expect(window && window.value() ==
				 "{\"open\":true,\"reason\":\"day_present\",\"sequence\":1,\"supervisor_session_id\":\"" +
					 kSession + "\",\"type\":\"window\",\"version\":1}\n",
	       "window frame is compact, sorted, newline-terminated");
	auto decoded = decode_supervisor_message(window.value());
	expect(decoded && std::get<WindowMessage>(decoded.value()).open, "window decodes");
	auto claim = encode(kClaim);
	auto request = decode_claim_request(claim.value());
	expect(request && request.value().touch_observation_id == kClaim.touch_observation_id &&
		       request.value().presentation_binding.committed_render_generation == 3,
	       "claim round trip");
	ClaimEnvelope envelope{kClaim.touch_observation_id, 9, kClaim.canonical_operation_digest, kBinding, 120};
	auto accepted = encode(ClaimResult{kSession, 2, kClaim.request_id, envelope, {}});
	auto result = decode_supervisor_message(accepted.value());
	expect(result && std::get<ClaimResult>(result.value()).envelope->contact_duration_milliseconds == 120,
	       "accepted claim result");
	auto refused = encode(ClaimResult{kSession, 3, kClaim.request_id, std::nullopt, "observation_unknown"});
	auto refusal = decode_supervisor_message(refused.value());
	expect(refusal && std::get<ClaimResult>(refusal.value()).error_code == "observation_unknown",
	       "refused claim result");
}

void test_codec_refusals()
{
	expect(!encode(WindowMessage{kSession, 1, true, WindowReason::Night}), "open must mean day_present");
	expect(!encode(WindowMessage{"short", 1, false, WindowReason::Night}), "session is 32 hex");
	expect(!encode(ClaimResult{kSession, 1, kClaim.request_id, std::nullopt, {}}), "a result needs an outcome");
	auto bad_digest = kClaim;
	bad_digest.canonical_operation_digest = "md5:00";
	expect(!encode(bad_digest), "digest must be sha256:64hex");
	const std::string window = encode(WindowMessage{kSession, 1, false, WindowReason::Night}).value();
	expect(!decode_supervisor_message(window.substr(0, window.size() - 1)), "missing newline");
	auto duplicated = window;
	duplicated.insert(1, "\"open\":false,");
	expect(!decode_supervisor_message(duplicated), "duplicate key");
	auto extra = window;
	extra.insert(1, "\"extra\":1,");
	expect(!decode_supervisor_message(extra), "unknown key");
	auto versioned = window;
	versioned.replace(versioned.find("\"version\":1"), 11, "\"version\":2");
	expect(!decode_supervisor_message(versioned), "other version");
	expect(!decode_supervisor_message(std::string(5000, ' ') + "\n"), "oversize frame");
	expect(!decode_claim_request(window), "a window is not a claim");
}

/** Writes one frame straight onto the socket, as a misbehaving peer would. */
void raw(int descriptor, const std::string &frame)
{
	if (::write(descriptor, frame.data(), frame.size()) != static_cast<ssize_t>(frame.size()))
		std::abort();
}

void test_client_window_and_sequence_rules()
{
	int pair[2];
	::socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, pair);
	expect(!MediaWindowClient::for_connected_peer(::dup(pair[1]), ::getuid() + 1), "another UID is refused");
	auto client = MediaWindowClient::for_connected_peer(pair[1], ::getuid());
	expect(client.has_value(), "same-UID peer accepted");
	auto &window = *client.value();
	expect(!window.window_open(), "closed until told otherwise");
	expect(!window.claim(kClaim) && window.connected(), "no claim while closed, and the channel stays");
	raw(pair[0], encode(WindowMessage{kSession, 1, true, WindowReason::DayPresent}).value());
	auto update = window.poll();
	expect(update && update.value().window_changed && window.window_open(), "window opens");
	expect(window.claim(kClaim).has_value(), "claim sent while open");
	char buffer[4096];
	const auto got = ::read(pair[0], buffer, sizeof buffer);
	expect(got > 0 && decode_claim_request(std::string(buffer, static_cast<std::size_t>(got))).has_value(),
	       "supervisor side reads a valid claim");
	raw(pair[0], encode(ClaimResult{kSession, 2, kClaim.request_id, std::nullopt, "observation_unknown"}).value());
	update = window.poll();
	expect(update && update.value().claim_results.size() == 1 && window.window_open(), "claim result delivered");
	raw(pair[0], encode(WindowMessage{kSession, 4, false, WindowReason::Absence}).value());
	auto gap = window.poll();
	expect(!gap && gap.error().code == "MEDIA_WINDOW.SEQUENCE_BROKEN" && !window.window_open() &&
		       !window.connected(),
	       "a sequence gap closes the window and the channel");
	::close(pair[0]);

	::socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, pair);
	auto second = MediaWindowClient::for_connected_peer(pair[1], ::getuid());
	raw(pair[0], encode(WindowMessage{kSession, 1, true, WindowReason::DayPresent}).value());
	raw(pair[0], encode(WindowMessage{std::string(32, 'f'), 2, true, WindowReason::DayPresent}).value());
	expect(!second.value()->poll() && !second.value()->window_open(), "a new session closes the window");
	::close(pair[0]);

	::socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, pair);
	auto third = MediaWindowClient::for_connected_peer(pair[1], ::getuid());
	raw(pair[0], encode(WindowMessage{kSession, 1, true, WindowReason::DayPresent}).value());
	expect(third.value()->poll() && third.value()->window_open(), "open");
	::close(pair[0]);
	expect(!third.value()->poll() && !third.value()->window_open(), "a lost supervisor closes the window");
	::socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, pair);
	auto fourth = MediaWindowClient::for_connected_peer(pair[1], ::getuid());
	raw(pair[0], "{\"type\":\"window\"}\n");
	expect(!fourth.value()->poll() && !fourth.value()->connected(), "a malformed frame closes the channel");
	::close(pair[0]);
}

void test_untrusted_paths()
{
	expect(!MediaWindowClient::connect("relative.sock", 0), "relative path");
	expect(!MediaWindowClient::connect("/tmp/no-such-media-window.sock", 0), "a world-writable parent");
}
} // namespace

int main()
{
	test_codec_round_trips();
	test_codec_refusals();
	test_client_window_and_sequence_rules();
	test_untrusted_paths();
	std::cout << "media_window_protocol: " << failures << " failures\n";
	return failures ? 1 : 0;
}
