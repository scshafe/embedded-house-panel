#include "panel_ui/panel_app_descriptor.hpp"
#include "simulator/panel_simulation.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool condition, const char *message) {
 if (!condition) throw std::runtime_error(message);
}

bool visible_text_contains(lv_obj_t *object, const std::string &text) {
 if (lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN)) return false;
 if (lv_obj_check_type(object, &lv_label_class) &&
     std::string(lv_label_get_text(object)).find(text) != std::string::npos) return true;
 for (std::uint32_t index = 0; index < lv_obj_get_child_count(object); ++index)
  if (visible_text_contains(lv_obj_get_child(object, index), text)) return true;
 return false;
}

std::vector<lv_obj_t *> labels_matching(lv_obj_t *object, const std::string &text) {
 std::vector<lv_obj_t *> found;
 if(lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN)) return found;
 if(lv_obj_check_type(object, &lv_label_class) && text == lv_label_get_text(object)) found.push_back(object);
 for(std::uint32_t index = 0; index < lv_obj_get_child_count(object); ++index) {
  auto children = labels_matching(lv_obj_get_child(object, index), text);
  found.insert(found.end(), children.begin(), children.end());
 }
 return found;
}
void tap_target(bhp::simulator::PanelSimulation &app, const std::string &intent) {
 const auto targets = app.display.hit_targets();
 const auto target = std::find_if(targets.begin(), targets.end(), [&](const auto &item) { return item.intent_id == intent; });
 require(target != targets.end(), "required navigation target must be visible");
 app.tap(target->x + target->width / 2, target->y + target->height / 2);
}
template <typename Action>
void rejects_with(Action action, const std::string &code) {
 try { action(); }
 catch (const std::runtime_error &error) {
  require(std::string(error.what()).find(code) != std::string::npos,
          "display rejection must retain its typed failure code");
  return;
 }
 throw std::runtime_error("invalid display text was accepted");
}
}

int main() {
 try {
  const auto root = std::filesystem::path(BHP_SIMULATOR_SOURCE_ROOT);
  std::ifstream stream(root / "simulator/tests/fixtures/display-text-cases.json");
  const auto cases = bhp::simulator::Json::parse(stream);
  bhp::simulator::PanelSimulation app(false);
  app.load(bhp::simulator::read_scenario((root / "simulator/scenarios/day-back.json").string()));
  auto scene = app.scene;
  scene.card_word = cases.at("supported_word");
  scene.card_definition = cases.at("supported_definition");
  scene.card_example = cases.at("supported_example");
  require(app.display.render(scene) > 0, "Latin text and punctuation render through the shell");

  scene.card_word = cases.at("unsupported_word");
  rejects_with([&] { app.display.render(scene); }, "GLYPH_UNSUPPORTED");
  scene.card_word = cases.at("overflow_word");
  rejects_with([&] { app.display.render(scene); }, "TEXT_LAYOUT_OVERFLOW");
  scene.card_word = cases.at("supported_word");
  const std::string definition = cases.at("supported_definition");
  for (int repeat = 0; repeat < 8; ++repeat) scene.card_definition += " " + definition;
  rejects_with([&] { app.display.render(scene); }, "TEXT_LAYOUT_OVERFLOW");
  scene.card_definition = definition;
  const std::string example = cases.at("supported_example");
  for (int repeat = 0; repeat < 8; ++repeat) scene.card_example += " " + example;
  rejects_with([&] { app.display.render(scene); }, "TEXT_LAYOUT_OVERFLOW");

  // Use the published hit geometry and its last pixel, rather than repeating
  // a visual coordinate in the input path. All four controls must be reachable.
  for (const auto &intent : {"study.rate.again", "study.rate.hard", "study.rate.good", "study.rate.easy"}) {
   app.load(bhp::simulator::read_scenario((root / "simulator/scenarios/day-back.json").string()));
   const auto targets = app.display.hit_targets();
   const auto found = std::find_if(targets.begin(), targets.end(), [&](const auto &target) {
    return target.intent_id == intent;
   });
   require(found != targets.end(), "every visible rating has a hit target");
   require(found->width >= 64 && found->height >= 64, "rating touch targets retain the approved minimum height");
   app.tap(found->x + found->width - 1, found->y + found->height / 2);
   require(app.scene.can_undo, "the rightmost pixel of each rating reaches the canonical reducer");
  }
  app.semantic_intent("study.rate.good", "touch");
  require(app.scene.acknowledgement == "STUDY.RATING_UNAVAILABLE", "the next front rejects an unavailable rating");
  auto *screen = lv_display_get_screen_active(app.display.display());
  require(visible_text_contains(screen, "Waiting to sync"), "action feedback cannot hide a pending review");
  require(visible_text_contains(screen, "Rating unavailable"), "action rejection remains visible beside pending review status");
  scene = app.scene;
  scene.stale = true;
  scene.offline = true;
  app.display.render(scene);
  require(visible_text_contains(screen, "Waiting to sync"), "offline/stale notices cannot hide pending review status");
  // The permanent day bar is identical across real feature surfaces and Home.
  scene = app.scene;
  scene.fixture_data = false;
  scene.clock_source = "fixture";
  scene.clock_previous_text.clear();
  scene.clock_transition_milli = 1000;
  scene.weather_available = false;
  scene.weather_fixture = false;
  scene.weather_stale = false;
  scene.stale = false;
  scene.offline = false;
  scene.acknowledgement.clear();
  scene.delivery_status.clear();
  std::vector<std::uint32_t> day_bar;
  std::vector<std::string> surfaces;
  for(const auto &app_descriptor : house_panel::panel_ui::panel_app_descriptors())
   if(app_descriptor.surface_id != "home") surfaces.push_back(app_descriptor.surface_id);
  surfaces.push_back("home");
  for(const auto &surface : surfaces) {
   scene.surface_id = surface;
   app.display.render(scene);
   require(visible_text_contains(screen, "Home"), "every day surface has the permanent Home control");
   for(const auto digit : scene.clock_text) require(!labels_matching(screen, std::string(1, digit)).empty(), "fixture clock digits remain visible with real study data");
   require(visible_text_contains(screen, "High") && visible_text_contains(screen, "Low"), "day bar names both forecast roles");
   require(labels_matching(screen, "—").size() == 2, "unavailable weather shows two explicit unknowns");
   require(visible_text_contains(screen, "Preview"), "independent fixture clock retains preview provenance");
   const auto &pixels = app.display.pixels();
   std::vector<std::uint32_t> bar(pixels.begin(), pixels.begin() + 1024 * 72);
   if(day_bar.empty()) day_bar = bar;
   else require(bar == day_bar, "feature routing must preserve every pixel of the day bar");
   const auto &targets = app.display.hit_targets();
   require(std::count_if(targets.begin(), targets.end(), [](const auto &target) { return target.intent_id == "shell.home"; }) == 1,
           "each day surface supplies exactly one Home route");
  }
  require(visible_text_contains(screen, "Lexinet") && visible_text_contains(screen, "Agenda") &&
          visible_text_contains(screen, "Voice lab"), "Home exposes the three real app surfaces");
  require(app.display.hit_targets().size() == 4, "Home must not advertise inactive placeholder apps");
  app.load(bhp::simulator::read_scenario((root / "simulator/scenarios/day-back.json").string()));
  tap_target(app, "shell.home");
  require(app.scene.surface_id == "home", "Home icon reaches the shell launcher");
  tap_target(app, "shell.open_study");
  require(app.scene.surface_id == "study" && app.scene.study_state == "back", "Lexinet launcher restores the existing study face");
  tap_target(app, "shell.home");
  tap_target(app, "shell.open_agenda");
  require(app.scene.surface_id == "agenda", "Agenda launcher reaches the fixture surface");
  require(visible_text_contains(screen, "Birthdays"), "Agenda routing renders real preview content");

  // Values determine colors, never the role named High or Low. F/C formatting
  // changes the displayed unit while retaining the same physical-temperature band.
  scene = app.scene;
  scene.surface_id = "study";
  scene.weather_available = true;
  scene.weather_fixture = true;
  scene.weather_temperature_unit = "F";
  scene.weather_high_celsius = -5;
  scene.weather_low_celsius = -10;
  app.display.render(scene);
  for(const auto &text : {"23°F", "14°F"}) {
   const auto labels = labels_matching(screen, text);
   require(labels.size() == 1, "forecast values must include the selected Fahrenheit unit");
   require(lv_color_eq(lv_obj_get_style_text_color(labels.front(), LV_PART_MAIN), lv_color_hex(0x7faee8)), "both freezing forecast values use the cold band");
  }
  scene.weather_high_celsius = 35;
  scene.weather_low_celsius = 32;
  app.display.render(scene);
  for(const auto &text : {"95°F", "90°F"}) {
   const auto labels = labels_matching(screen, text);
   require(labels.size() == 1, "warm forecast values convert to Fahrenheit");
   require(lv_color_eq(lv_obj_get_style_text_color(labels.front(), LV_PART_MAIN), lv_color_hex(0xec8b78)), "both hot forecast values use the hot band");
  }
  scene.weather_temperature_unit = "C";
  const std::vector<std::pair<int, std::uint32_t>> bands{{0, 0x7faee8}, {10, 0x79c5d6}, {20, 0x9bc996}, {30, 0xe9c480}, {31, 0xec8b78}};
  for(const auto &band : bands) {
   scene.weather_high_celsius = scene.weather_low_celsius = band.first;
   app.display.render(scene);
   const auto labels = labels_matching(screen, std::to_string(band.first) + "°C");
   require(labels.size() == 2, "Celsius values appear under both forecast roles");
   for(auto *value : labels)
    require(lv_color_eq(lv_obj_get_style_text_color(value, LV_PART_MAIN), lv_color_hex(band.second)), "equal temperatures use equal colors regardless of High/Low role");
  }
  scene.weather_stale = true;
  scene.delivery_status = "Waiting to sync";
  app.display.render(scene);
  require(visible_text_contains(screen, "Weather stale") && visible_text_contains(screen, "Waiting to sync"), "weather staleness and pending review are independently visible");
  scene.weather_available = false;
  scene.weather_stale = false;
  scene.weather_fixture = false;
  scene.clock_source = "unavailable";
  scene.fixture_data = false;
  app.display.render(scene);
  require(labels_matching(screen, "—").size() == 3, "unavailable time and forecast cannot synthesize plausible values");
  require(!visible_text_contains(screen, "Preview"), "real study and unavailable bar sources do not acquire a fixture label");

  scene.clock_source = "fixture";
  scene.clock_text = "07:42";
  scene.clock_previous_text.clear();
  scene.clock_transition_milli = 1000;
  scene.lifecycle.display_mode = bhp::panel_mode_protocol::DisplayMode::NightRed;
  scene.lifecycle.solar_state = bhp::panel_mode_protocol::SolarState::Night;
  scene.lifecycle.system_state = bhp::panel_mode_protocol::SystemState::Active;
  scene.night_red_rgb888 = 0x180000;
  app.display.render(scene);
  const auto &night_pixels = app.display.pixels();
  require(app.display.hit_targets().empty(), "night has no interaction targets");
  require(std::count_if(night_pixels.begin(), night_pixels.end(), [](std::uint32_t pixel) { return (pixel & 0xffffff) == 0x180000; }) > 500000, "the night field fills the full screen with configured dim red");
  const auto black_pixels = std::count_if(night_pixels.begin(), night_pixels.end(), [](std::uint32_t pixel) { return (pixel & 0xffffff) == 0; });
  require(black_pixels > 1000 && black_pixels < 100000, "large black clock glyphs are visible against the red field");
  require(std::all_of(night_pixels.begin(), night_pixels.end(), [](std::uint32_t pixel) { return (pixel & 0x00ffff) == 0 && (pixel & 0xffffff) <= 0x180000; }),
          "night antialiasing contains only black through configured red");
  scene.clock_text = "24:00";
  rejects_with([&] { app.display.render(scene); }, "CLOCK_FRAME_INVALID");
  scene.clock_text = "07:60";
  rejects_with([&] { app.display.render(scene); }, "CLOCK_FRAME_INVALID");
  scene.clock_text = "07:42";
  scene.clock_transition_milli = 1001;
  rejects_with([&] { app.display.render(scene); }, "CLOCK_FRAME_INVALID");
  scene.clock_transition_milli = 500;
  scene.clock_previous_text = "学習";
  rejects_with([&] { app.display.render(scene); }, "FONT_GLYPH_UNSUPPORTED");
  scene.clock_previous_text = "12:99";
  rejects_with([&] { app.display.render(scene); }, "CLOCK_FRAME_INVALID");
  scene.clock_previous_text.clear();
  scene.clock_transition_milli = 1000;
  scene.clock_source = "unavailable";
  app.display.render(scene);
  require(std::all_of(app.display.pixels().begin(), app.display.pixels().end(), [](std::uint32_t pixel) { return (pixel & 0xffffff) == 0; }), "unavailable night source remains fail dark");
  scene.clock_source = "unknown";
  app.display.render(scene);
  require(std::all_of(app.display.pixels().begin(), app.display.pixels().end(), [](std::uint32_t pixel) { return (pixel & 0xffffff) == 0; }), "unknown clock source cannot acquire fixture trust");
  scene.clock_source = "fixture";
  scene.fixture_clock_untrusted = true;
  app.display.render(scene);
  require(std::all_of(app.display.pixels().begin(), app.display.pixels().end(), [](std::uint32_t pixel) { return (pixel & 0xffffff) == 0; }), "untrusted night time remains fail dark");
  scene.fixture_clock_untrusted = false;
  scene.lifecycle.display_mode = bhp::panel_mode_protocol::DisplayMode::Diagnostic;
  app.display.render(scene);
  require(app.display.hit_targets().empty(), "diagnostic mode exposes no day-bar or app targets");
  scene.lifecycle.display_mode = bhp::panel_mode_protocol::DisplayMode::Suspended;
  app.display.render(scene);
  require(app.display.hit_targets().empty(), "suspended mode exposes no interaction targets");
  scene.lifecycle.display_mode = bhp::panel_mode_protocol::DisplayMode::DayActive;
  scene.presentation_failure = true;
  app.display.render(scene);
  require(app.display.hit_targets().empty(), "failed presentation exposes no interaction targets");
  std::cout << "{\"passed\":true,\"checks\":[\"unicode_rendering\",\"unsupported_glyph\",\"text_overflow\",\"rating_hit_edges\",\"home_routing\",\"permanent_day_bar\",\"temperature_colors_units\",\"night_red_black_clock\",\"lifecycle_hit_gates\"],\"evidence\":\"simulation_not_hardware_evidence\"}\n";
  return 0;
 } catch (const std::exception &error) {
  std::cerr << error.what() << '\n';
  return 1;
 }
}
