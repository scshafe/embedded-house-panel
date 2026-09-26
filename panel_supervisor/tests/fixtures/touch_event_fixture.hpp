#pragma once
#include "panel_supervisor/evdev_touch_source.hpp"
#include <stdexcept>

namespace house_panel::supervisor::fixtures {
inline TouchEvdevConfiguration tablet() {
	return {"/dev/input/fixture", "fixture-tablet", TouchEvdevProtocol::SingleContact,
		0, 32767, 0, 32767, 272, 1};
}
inline TouchEvdevConfiguration multitouch() {
	auto config = tablet(); config.protocol = TouchEvdevProtocol::TypeB;
	config.slot_count = 10; config.contact_key = 0; config.exact_name = "fixture-multitouch"; return config;
}
inline TypeBKernelState idle_type_b_kernel_state(std::int32_t selected_slot = 0) {
	TypeBKernelState state{selected_slot, multitouch().slot_count, {}};
	for (std::uint32_t i = 0; i < state.slot_count; ++i)
		state.slots[i] = {-1, 500 + static_cast<std::int32_t>(i), 600 + static_cast<std::int32_t>(i)};
	return state;
}
inline void event(EvdevTouchParser &parser, std::uint16_t type, std::uint16_t code, std::int32_t value) {
	if (!parser.consume(type, code, value)) throw std::runtime_error("fixture event rejected");
}
inline void position(EvdevTouchParser &parser, bool mt, int x = 500, int y = 600) {
	event(parser, 3, mt ? 53 : 0, x); event(parser, 3, mt ? 54 : 1, y);
}
}
