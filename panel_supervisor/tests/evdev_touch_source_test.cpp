#include "fixtures/touch_event_fixture.hpp"
#include "panel_supervisor/touch_runtime_configuration.hpp"
#include <iostream>

using namespace house_panel::supervisor;
using namespace house_panel::supervisor::fixtures;
using State = bhp::touch_observation_protocol::ContactState;
#define REQUIRE(c) do { if (!(c)) { std::cerr << __LINE__ << ": " #c "\n"; return 1; } } while (false)

int main() {
	auto single = EvdevTouchParser::create(tablet()); REQUIRE(single);
	auto &p = single.value();
	event(p, 0, 0, 0); position(p, false); event(p, 1, 272, 1);
	auto start = p.consume(0, 0, 0); REQUIRE(start && start.value() && start.value()->state == State::Started);
	position(p, false, 1000, 2000); event(p, 1, 272, 0);
	auto finish = p.consume(0, 0, 0); REQUIRE(finish && finish.value() && finish.value()->state == State::Completed && finish.value()->x == 1000);
	// A held contact cancelled by a lifecycle cannot finish on a different frame.
	event(p, 1, 272, 1); event(p, 0, 0, 0); p.cancel_until_idle(); event(p, 1, 272, 0);
	auto cancelled = p.consume(0, 0, 0); REQUIRE(cancelled && !cancelled.value());
	event(p, 1, 272, 1); auto restarted = p.consume(0, 0, 0); REQUIRE(restarted && restarted.value());
	REQUIRE(!p.consume(0, 3, 0)); REQUIRE(!p.consume(0, 0, 0));
	// Explicit Type B slots accept a single contact and reject overlap until idle.
	auto multi = EvdevTouchParser::create(multitouch()); REQUIRE(multi); auto &m = multi.value();
	event(m, 0, 0, 0); event(m, 3, 47, 2); event(m, 3, 57, 15); position(m, true);
	auto first = m.consume(0, 0, 0); REQUIRE(first && first.value() && first.value()->state == State::Started);
	event(m, 3, 47, 3); event(m, 3, 57, 16); position(m, true);
	REQUIRE(!m.consume(0, 0, 0));
	event(m, 3, 57, -1); event(m, 3, 47, 2); event(m, 3, 57, -1);
	auto released = m.consume(0, 0, 0); REQUIRE(released && !released.value());
	event(m, 3, 57, 17); position(m, true); auto next = m.consume(0, 0, 0); REQUIRE(next && next.value());
	REQUIRE(!m.consume(3, 47, 10));
	auto invalid = tablet(); invalid.x_maximum = 0; REQUIRE(!EvdevTouchParser::create(invalid));
	auto axes = EvdevTouchParser::create(tablet()); REQUIRE(!axes.value().consume(3, 0, 32768));
	REQUIRE(TouchRuntimeConfiguration::parse({}));
	REQUIRE(!TouchRuntimeConfiguration::parse({{"BHP_PANEL_SUPERVISOR_TOUCH_PROTOCOL", "single"}}));
	const std::array<std::string, 13> configured_values{"/dev/input/fixture", "fixture-tablet", "single",
		"0", "32767", "0", "32767", "272", "1", "/run/bhp/fixture.sock", "1500", "4000", "128"};
	std::map<std::string, std::string> configured;
	for (std::size_t i = 0; i < configured_values.size(); ++i) configured[kTouchRuntimeEnvironmentFields[i]] = configured_values[i];
	auto enabled = TouchRuntimeConfiguration::parse(configured);
	REQUIRE(enabled && enabled.value() && enabled.value()->device.contact_key == 272);
	configured["BHP_PANEL_SUPERVISOR_TOUCH_TTL_MILLISECONDS"] = "5001";
	REQUIRE(!TouchRuntimeConfiguration::parse(configured));
	std::cout << "{\"test\":\"evdev_touch_source\",\"result\":\"passed\",\"evidence\":\"host_software_only\"}\n";
}
