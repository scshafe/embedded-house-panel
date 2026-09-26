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
	// A reopened idle device may send only a new tracking ID: all slot axes and
	// the actual selected kernel slot must survive initialization, not the last
	// slot visited while reading the snapshot. Repeat with identical coordinates.
	for (const auto selected_slot : {0, 3}) {
		const auto snapshot = idle_type_b_kernel_state(selected_slot);
		for (int reopen = 0; reopen < 2; ++reopen) {
			auto seeded = EvdevTouchParser::create(multitouch()); REQUIRE(seeded);
			auto &parser = seeded.value(); REQUIRE(parser.initialize_type_b_state(snapshot));
			event(parser, 3, 57, 25);
			auto began = parser.consume(0, 0, 0);
			REQUIRE(began && began.value() && began.value()->state == State::Started);
			REQUIRE(began.value()->x == 500 + selected_slot && began.value()->y == 600 + selected_slot);
			event(parser, 3, 57, -1);
			auto ended = parser.consume(0, 0, 0);
			REQUIRE(ended && ended.value() && ended.value()->state == State::Completed);
			REQUIRE(ended.value()->x == 500 + selected_slot && ended.value()->y == 600 + selected_slot);
			// A different idle slot was also seeded, even though it was unselected.
			event(parser, 3, 47, 8); event(parser, 3, 57, 26);
			auto other_slot = parser.consume(0, 0, 0);
			REQUIRE(other_slot && other_slot.value() && other_slot.value()->x == 508 && other_slot.value()->y == 608);
		}
	}
	// A contact held when the kernel snapshot was read must release. Neither
	// initialization nor a lifecycle cancellation may turn that hold into a tap.
	auto held_snapshot = idle_type_b_kernel_state(2); held_snapshot.slots[2].tracking_id = 41;
	auto held_parser = EvdevTouchParser::create(multitouch()); REQUIRE(held_parser);
	auto &held = held_parser.value(); REQUIRE(held.initialize_type_b_state(held_snapshot));
	auto held_report = held.consume(0, 0, 0); REQUIRE(held_report && !held_report.value());
	event(held, 3, 57, -1);
	auto held_release = held.consume(0, 0, 0); REQUIRE(held_release && !held_release.value());
	event(held, 3, 57, 42);
	auto fresh_contact = held.consume(0, 0, 0);
	REQUIRE(fresh_contact && fresh_contact.value() && fresh_contact.value()->state == State::Started);
	held.cancel_until_idle();
	auto cancelled_hold = held.consume(0, 0, 0); REQUIRE(cancelled_hold && !cancelled_hold.value());
	event(held, 3, 57, -1);
	auto cancelled_release = held.consume(0, 0, 0); REQUIRE(cancelled_release && !cancelled_release.value());
	event(held, 3, 57, 43);
	auto after_cancel = held.consume(0, 0, 0);
	REQUIRE(after_cancel && after_cancel.value() && after_cancel.value()->state == State::Started);
	// Snapshot validation covers every idle slot as well as held contacts. A bad
	// snapshot poisons this parser; later events or reseeding cannot recover it.
	const auto valid_snapshot = idle_type_b_kernel_state();
	std::array<TypeBKernelState, 11> invalid_snapshots{}; invalid_snapshots.fill(valid_snapshot);
	invalid_snapshots[0].slot_count = 0;
	invalid_snapshots[1].slot_count = valid_snapshot.slot_count + 1;
	invalid_snapshots[2].slot_count = 33;
	invalid_snapshots[3].selected_slot = -1;
	invalid_snapshots[4].selected_slot = static_cast<std::int32_t>(valid_snapshot.slot_count);
	invalid_snapshots[5].slots[9].tracking_id = -2;
	invalid_snapshots[6].slots[9].x = -1;
	invalid_snapshots[7].slots[9].x = 32768;
	invalid_snapshots[8].slots[9].y = -1;
	invalid_snapshots[9].slots[9].y = 32768;
	invalid_snapshots[10].slots[0].tracking_id = 7; invalid_snapshots[10].slots[1].tracking_id = 7;
	for (const auto &snapshot : invalid_snapshots) {
		auto rejected = EvdevTouchParser::create(multitouch()); REQUIRE(rejected);
		REQUIRE(!rejected.value().initialize_type_b_state(snapshot));
		REQUIRE(!rejected.value().consume(0, 0, 0));
		REQUIRE(!rejected.value().initialize_type_b_state(valid_snapshot));
	}
	auto repeated_seed = EvdevTouchParser::create(multitouch()); REQUIRE(repeated_seed);
	REQUIRE(repeated_seed.value().initialize_type_b_state(valid_snapshot));
	REQUIRE(!repeated_seed.value().initialize_type_b_state(valid_snapshot));
	REQUIRE(!repeated_seed.value().consume(0, 0, 0));
	auto late_seed = EvdevTouchParser::create(multitouch()); REQUIRE(late_seed);
	event(late_seed.value(), 0, 0, 0);
	REQUIRE(!late_seed.value().initialize_type_b_state(valid_snapshot));
	auto wrong_protocol = EvdevTouchParser::create(tablet()); REQUIRE(wrong_protocol);
	REQUIRE(!wrong_protocol.value().initialize_type_b_state(valid_snapshot));
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
