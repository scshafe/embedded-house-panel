#include "simulator/panel_simulation.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

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
  std::cout << "{\"passed\":true,\"checks\":[\"unicode_rendering\",\"unsupported_glyph\",\"text_overflow\",\"rating_hit_edges\"],\"evidence\":\"simulation_not_hardware_evidence\"}\n";
  return 0;
 } catch (const std::exception &error) {
  std::cerr << error.what() << '\n';
  return 1;
 }
}
