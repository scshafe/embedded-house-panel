// The music-control frames: closed key sets both ways, bounds, and the client's
// exact-UID check over a socket pair.
#include "music_control/music_protocol.h"
#include <iostream>
#include <sys/socket.h>
#include <unistd.h>
using namespace music_control::protocol;
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

State playing()
{
	State state;
	state.sequence = 7;
	state.sign_in = "signed_in";
	state.output = {"active", "Living Room Bose", true, 35u};
	state.playback = Playback{true, "Slow Sunrise — Live", "The Fixture Band, Café Ensemble"};
	return state;
}

void requests()
{
	for (const UiRequest &request : {UiRequest(StatusRequest{}), UiRequest(SelectOutputRequest{}), UiRequest(PlayRequest{}),
					 UiRequest(PauseRequest{}), UiRequest(NextRequest{}), UiRequest(VolumeRequest{42})}) {
		auto frame = encode(request);
		auto decoded = frame.has_value() ? decode_ui_request(frame.value()) : music_control::Result<UiRequest>::failure({});
		expect(decoded.has_value() && decoded.value().index() == request.index(),
		       "round trip: " + request_name(request));
	}
	expect(encode(UiRequest(VolumeRequest{42})).value() == "{\"percent\":42,\"type\":\"volume\",\"version\":1}\n",
	       "the volume frame");
	expect(!encode(UiRequest(VolumeRequest{101})).has_value(), "volume above 100 is not encoded");
	for (const char *bad : {"{\"type\":\"volume\",\"version\":1,\"percent\":101}\n",
				"{\"type\":\"volume\",\"version\":1,\"percent\":-1}\n",
				"{\"type\":\"play\",\"version\":1,\"device_id\":\"x\"}\n", "{\"type\":\"play\",\"version\":2}\n",
				"{\"type\":\"play\",\"version\":1}", "{\"type\":\"play\",\"version\":1}\n{}\n",
				"{\"type\":\"transfer\",\"version\":1}\n"})
		expect(!decode_ui_request(bad).has_value(), std::string("refused request: ") + bad);
}

void events()
{
	auto frame = encode(MusicEvent(playing()));
	expect(frame.has_value(), "a playing state encodes");
	auto decoded = decode_music_event(frame.value());
	expect(decoded.has_value() && std::holds_alternative<State>(decoded.value()), "a playing state decodes");
	if (decoded.has_value()) {
		const auto &state = std::get<State>(decoded.value());
		expect(state.sequence == 7 && state.output.name == "Living Room Bose" && state.output.volume_percent == 35u &&
			       state.playback && state.playback->title == "Slow Sunrise — Live",
		       "the playing state round trips");
	}
	State idle;
	idle.sequence = 1;
	idle.output.name = "Living Room Bose";
	auto idle_frame = encode(MusicEvent(idle));
	expect(idle_frame.has_value() && idle_frame.value().find("\"playback\":null") != std::string::npos &&
		       idle_frame.value().find("\"volume_percent\":null") != std::string::npos,
	       "absent playback and volume are null");

	auto inactive = playing();
	inactive.output.state = "available";
	expect(!encode(MusicEvent(inactive)).has_value(), "playback only while the speaker is active");
	auto nameless = playing();
	nameless.output.name.clear();
	expect(!encode(MusicEvent(nameless)).has_value(), "the speaker name is required");
	auto control = playing();
	control.playback->title = "a\nb";
	expect(!encode(MusicEvent(control)).has_value(), "control characters are refused");
	auto long_title = playing();
	long_title.playback->title.assign(kMaximumTextBytes + 1, 'x');
	expect(!encode(MusicEvent(long_title)).has_value(), "over-long text is refused");
	auto bad_error = playing();
	bad_error.error = "not a code";
	expect(!encode(MusicEvent(bad_error)).has_value(), "the error is a code");

	auto refused = encode(MusicEvent(Refused{"volume", "MUSIC.VOLUME_UNSUPPORTED"}));
	expect(refused.has_value() && decode_music_event(refused.value()).has_value(), "refused round trips");
	expect(!encode(MusicEvent(Refused{"status", "MUSIC.X"})).has_value(), "status is never refused");
	expect(!decode_music_event(
			"{\"type\":\"state\",\"version\":1,\"sequence\":1,\"sign_in\":\"signed_in\",\"output\":{\"state\":\"active\","
			"\"name\":\"B\",\"supports_volume\":true,\"volume_percent\":5,\"device_id\":\"x\"},\"playback\":null,\"error\":null}\n")
			.has_value(),
	       "an extra key in output is refused");
}

void client()
{
	int pair[2];
	::socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, pair);
	auto wrong = MusicClient::for_connected_peer(pair[0], ::getuid() + 1);
	expect(!wrong.has_value() && wrong.error().code == "MUSIC.CONTROL_PEER_REJECTED", "another UID is rejected");
	::close(pair[1]);
	::socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, pair);
	auto connected = MusicClient::for_connected_peer(pair[0], ::getuid());
	expect(connected.has_value(), "the exact UID is accepted");
	if (!connected.has_value())
		return;
	auto &music = *connected.value();
	expect(music.send(PauseRequest{}).has_value(), "send");
	char buffer[256];
	const auto got = ::read(pair[1], buffer, sizeof buffer);
	expect(got > 0 && std::string(buffer, static_cast<std::size_t>(got)) == "{\"type\":\"pause\",\"version\":1}\n",
	       "the pause frame on the wire");
	const auto frame = encode(MusicEvent(playing())).value();
	(void)::write(pair[1], frame.data(), frame.size());
	auto polled = music.poll();
	expect(polled.has_value() && polled.value().size() == 1, "poll returns the state");
	(void)::write(pair[1], "{\"type\":\"state\"}\n", 17);
	expect(!music.poll().has_value() && !music.connected(), "a malformed frame closes the client");
	::close(pair[1]);
}
} // namespace

int main()
{
	requests();
	events();
	client();
	if (failures == 0)
		std::cout << "music_control_protocol_test passed\n";
	return failures == 0 ? 0 : 1;
}
