#pragma once
#include "bhp/panel_mode_protocol/types.h"
#include "panel_ui/application_catalog.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace house_panel::panel_ui {
struct PanelHitTarget {
 int x; int y; int width; int height; std::string intent_id;
};
/** Immutable fixture presentation; lifecycle always comes from a protocol-v2 snapshot. */
struct PanelScene {
 bhp::panel_mode_protocol::Snapshot lifecycle;
 std::string scenario_id;
 std::string surface_id{"study"};
 ApplicationCatalog applications;
 std::size_t home_page{0};
 std::string clock_text{"07:42"};
 // Clock/forecast provenance is independent of the study transport or content.
 std::string clock_source{"fixture"};
 std::string clock_previous_text;
 std::uint16_t clock_transition_milli{1000};
 bool weather_available{false};
 bool weather_stale{false};
 bool weather_fixture{false};
 int weather_high_celsius{0};
 int weather_low_celsius{0};
 std::string weather_temperature_unit{"F"};
 std::string study_state{"front"};
 std::string card_word{"serendipity"};
 std::string card_definition{"The discovery of something valuable when you were looking for something else."};
 std::string card_example{"A chance conversation led to an unexpected friendship."};
 std::string delivery_status;
 std::string agenda_state{"today"};
 std::string acknowledgement;
 bool stale{false};
 bool offline{false};
 bool provisional{false};
 bool can_back{false};
 bool can_undo{false};
 bool presentation_failure{false};
 bool fixture_clock_untrusted{false};
 // Synthetic scenes and legacy simulation configs stay labeled until a data
 // source explicitly declares fixture_data=false. This is not transport inference.
 bool fixture_data{true};
 std::uint32_t night_red_rgb888{0x180000};
};
} // namespace house_panel::panel_ui
