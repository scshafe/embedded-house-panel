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
 /** One decimal, as the forecast gives it (ADR 0015); the bar rounds once, in the chosen unit. */
 double weather_high_celsius{0};
 double weather_low_celsius{0};
 /** F, C or K. */
 std::string weather_temperature_unit{"F"};
 /** "temperatures" (high and low) or "sun_times" (sunrise and sunset, the place's local HH:MM). */
 std::string weather_bar{"temperatures"};
 std::string weather_sunrise{"06:40"};
 std::string weather_sunset{"16:48"};
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
  /** The prompt on the screen, as the uploader's catalog names it. */
  std::string prompt_id{"reference-neutral-01"};
  /** The D-021 approval preview the owner reviews before confirming it. */
  std::string approval_endpoint{"voice-lab.example.ts.net"};
  std::string approval_node{"nEXAMPLE11CNTRL"};
  std::string approval_expires{"2027-10-19"};
  std::string approval_digest{"e5b522fd1ff6b4561e5d518b0e72906f30cb58b89c43d538b00c2343e7056114"};
  /** Why uploads wait, in plain words, or empty. */
  std::string notice;
 } voice_lab;
 /** The music remote's fixture or live presentation (Spotify Connect, ADR 0013). */
 struct Music {
  std::string state{"ready"};
  /** The configured speaker's Spotify device name. */
  std::string speaker{"Living Room Bose"};
  std::string title{"Slow Sunrise"};
  std::string artists{"The Fixture Band"};
  /** 0 to 100, or -1 when Spotify reports none. */
  int volume_percent{35};
  bool supports_volume{true};
  /** Why the last request failed, in plain words, or empty. */
  std::string notice;
 } music;
 /** The report screen (bug-reports-and-self-repair.md, step 2): fixture or live from bhp-reporter. */
 struct Report {
  std::string state{"compose"};
  /** What has been typed: the keys give lowercase ASCII only. */
  std::string text{"skip did nothing on the music screen"};
  /** Why the last request failed, in plain words, or empty. */
  std::string notice;
  /** The reports list, one line each, newest first. */
  std::vector<std::string> lines{"Typed report · Fix in PR #81 · 2026-11-08", "Crash · Received · 2026-11-07"};
  bool update_ready{false};
  /** The saved report's ID, as the sent screen shows it. */
  std::string sent_id{"rpt-0123456789abcdef0123456789abcdef"};
  /** A spoken description is recorded and will go with Send. */
  bool has_recording{false};
  /** While recording: how long so far. */
  std::uint64_t elapsed_ms{12000};
 } report;
 /** The weather menu (ADR 0015): its state and what it shows; fixture or live from weather_panel. */
 struct Weather {
  std::string state{"menu"};
  /** "Automatic (approximate)" or the chosen place; the line under the Location row. */
  bool automatic{true};
  std::string location_label{"Boulder, Colorado"};
  /** The search being typed: lowercase letters, space, hyphen and apostrophe. */
  std::string query{"boulder"};
  /** Up to five places, one line each, as the search answered. */
  std::vector<std::string> results{"Boulder, Colorado, United States", "Boulder, Montana, United States",
                                   "Boulder, Wyoming, United States", "Boulder, Western Cape, South Africa",
                                   "Boulder, Illinois, United States"};
  std::string notice;
 } weather;
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
