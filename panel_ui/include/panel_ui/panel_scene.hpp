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
 /** The voice-lab recorder's fixture or live presentation (issue #23). */
 struct VoiceLab {
  std::string state{"ready"};
  std::string prompt_text{"The quick brown fox jumps over the lazy dog."};
  std::size_t prompt_number{3};
  std::size_t prompt_total{40};
  std::size_t missing{38};
  std::string verification_uri{"id.example.ts.net/device"};
  std::string user_code{"EABCDEFG"};
  /** Peak per channel in tenths of a dBFS, -1200 for silence. */
  std::vector<int> level_tenths{-180, -240, -420, -410, -430, -415};
  std::uint64_t elapsed_ms{12000};
  std::uint64_t maximum_ms{600000};
  std::uint64_t take_ms{8400};
  int take_peak_tenths{-120};
  std::size_t queued_takes{2};
  /** The committed take under review; empty in fixtures. */
  std::string take_id;
 } voice_lab;
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
