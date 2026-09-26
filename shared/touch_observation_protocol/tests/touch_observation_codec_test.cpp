#include "bhp/touch_observation_protocol/touch_observation_codec.h"
#include <iostream>
#include <limits>
using namespace bhp::touch_observation_protocol;
#define REQUIRE(c) do { if (!(c)) { std::cerr << __LINE__ << ": " #c "\n"; return 1; } } while (false)
NormalizedTouchObservation fixture() {
	return {1, "fixture-observation-000001", 1, {2, std::string(32, 'a'), 2,
		"sha256:" + std::string(64, 'b'), std::string(32, 'c'), 3}, -100, 200,
		ContactState::Completed, 150};
}
int main() {
	auto value = fixture(); auto frame = encode_touch_observation(value); REQUIRE(frame);
	auto decoded = decode_touch_observation(frame.value()); REQUIRE(decoded);
	REQUIRE(decoded.value().raw_axis_x == -100 && decoded.value().supervisor_measured_contact_duration_milliseconds == 150);
	REQUIRE(!decode_touch_observation(frame.value().substr(0, frame.value().size() - 1)));
	REQUIRE(!decode_touch_observation(frame.value() + frame.value()));
	REQUIRE(!decode_touch_observation(std::string(kMaximumTouchFrameBytes + 1, 'a')));
	auto extra = frame.value(); extra.insert(extra.size() - 2, ",\"extra\":1"); REQUIRE(!decode_touch_observation(extra));
	auto duplicate = frame.value(); duplicate.insert(1, "\"raw_axis_x\":3,"); REQUIRE(!decode_touch_observation(duplicate));
	auto leading_zero = frame.value(); leading_zero.insert(leading_zero.find(":1") + 1, "0"); REQUIRE(!decode_touch_observation(leading_zero));
	value.supervisor_measured_contact_duration_milliseconds = 0; REQUIRE(!encode_touch_observation(value));
	value.contact_state = ContactState::Started; REQUIRE(encode_touch_observation(value));
	value.raw_axis_x = std::numeric_limits<std::int64_t>::max(); REQUIRE(!encode_touch_observation(value));
	std::cout << "{\"test\":\"touch_observation_codec\",\"result\":\"passed\"}\n";
}
