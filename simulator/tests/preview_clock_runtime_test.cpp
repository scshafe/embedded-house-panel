#include "simulator/panel_simulation.hpp"
#include "fixtures/preview_clock_runtime_cases.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
using bhp::simulator::PanelSimulation;
using bhp::simulator::Json;
namespace fixtures = bhp::simulator::fixtures;
using SteadyClock = std::chrono::steady_clock;

void require(bool condition, const char *message) {
 if(!condition) throw std::runtime_error(message);
}
Json scenario(const char *name) {
 const auto path = std::filesystem::path(BHP_SIMULATOR_SOURCE_ROOT) / "simulator/scenarios" / (std::string(name) + ".json");
 return bhp::simulator::read_scenario(path.string());
}
void service_for(PanelSimulation &app, std::chrono::milliseconds duration) {
 const auto deadline = SteadyClock::now() + duration;
 do {
  std::this_thread::sleep_for(fixtures::preview_clock_poll_interval);
  app.service();
 } while(SteadyClock::now() < deadline);
}
bool all_black(const PanelSimulation &app) {
 return std::all_of(app.display.pixels().begin(), app.display.pixels().end(),
                    [](std::uint32_t pixel) { return (pixel & 0xffffff) == 0; });
}
void require_settled(const PanelSimulation &app) {
 require(app.scene.clock_previous_text.empty() && app.scene.clock_transition_milli == 1000,
         "a cancelled or completed clock transition must have no outgoing digits");
}

void test_clock_step_publishes_generations_until_settled() {
 PanelSimulation app(false);
 app.load(scenario(fixtures::preview_clock_day_scenario));
 const auto initial_generation = app.render_generation;
 app.event({{"type", "clock_step"}});
 require(app.scene.clock_text == fixtures::preview_clock_day_successor &&
         app.scene.clock_previous_text == fixtures::preview_clock_day_start &&
         app.scene.clock_transition_milli == 0,
         "clock-step begins at the old fixture minute with a new destination");
 require(app.render_generation > initial_generation, "starting the reel publishes an ordinary render generation");
 const auto start_generation = app.render_generation;
 const auto start_pixels = app.display.pixels();
 const auto deadline = SteadyClock::now() + fixtures::preview_clock_settle_budget;
 bool saw_intermediate_generation = false;
 while(!app.scene.clock_previous_text.empty() && SteadyClock::now() < deadline) {
  std::this_thread::sleep_for(fixtures::preview_clock_poll_interval);
  const auto previous_generation = app.render_generation;
  app.service();
  if(app.scene.clock_transition_milli > 0 && app.scene.clock_transition_milli < 1000) {
   require(app.render_generation > previous_generation || app.render_generation > start_generation,
           "intermediate reel progress belongs to a fresh render generation");
   saw_intermediate_generation = true;
  }
 }
 require_settled(app);
 require(saw_intermediate_generation && app.render_generation > start_generation + 1,
         "service advances the visible reel through intermediate render generations");
 require(app.display.pixels() != start_pixels, "the settled minute replaces the outgoing minute pixels");
 const auto settled_generation = app.render_generation;
 const auto settled_pixels = app.display.pixels();
 service_for(app, fixtures::preview_clock_observe_stable);
 require(app.render_generation == settled_generation && app.display.pixels() == settled_pixels,
         "a disabled preview clock stops generating frames after its explicit step settles");
}

void test_lifecycle_changes_cancel_motion_without_replay() {
 PanelSimulation app(false);
 app.load(scenario(fixtures::preview_clock_day_scenario));
 app.enable_preview_clock(true);
 for(const auto *mode : fixtures::preview_clock_cancel_modes) {
  app.event({{"type", "clock_step"}});
  require(!app.scene.clock_previous_text.empty(), "fixture step starts a transition before the lifecycle change");
  app.event({{"type", "lifecycle"}, {"state", mode}});
  require_settled(app);
  const auto generation = app.render_generation;
  const auto pixels = app.display.pixels();
  service_for(app, fixtures::preview_clock_observe_stable);
  require_settled(app);
  require(app.render_generation == generation && app.display.pixels() == pixels,
          "blank, night and day transitions must not replay cancelled clock motion");
  if(std::string(mode) == "blank")
   require(all_black(app) && app.display.hit_targets().empty(), "blank mode stays black and has no input targets");
  if(std::string(mode) == "night")
   require(app.display.hit_targets().empty(), "night mode exposes no application input targets");
 }
 app.enable_preview_clock(false);
}

void test_frozen_reels_change_only_the_clock_pixels() {
 PanelSimulation app(false);
 for(const auto &fixture : fixtures::preview_clock_pixel_cases) {
  app.load(scenario(fixture.start_scenario));
  const auto start = app.display.pixels();
  app.load(scenario(fixture.midpoint_scenario));
  const auto middle = app.display.pixels();
  const auto midpoint_generation = app.render_generation;
  const auto midpoint_progress = app.scene.clock_transition_milli;
  service_for(app, fixtures::preview_clock_observe_frozen);
  require(app.render_generation == midpoint_generation && app.display.pixels() == middle &&
          app.scene.clock_transition_milli == midpoint_progress,
          "a frozen fixture frame stays deterministic beyond the runtime transition duration");
  app.load(scenario(fixture.settled_scenario));
  const auto end = app.display.pixels();
  require(middle != start && middle != end && start != end,
          "day and night midpoint frames differ from both reel endpoints");
  require(middle.size() == static_cast<std::size_t>(fixtures::preview_clock_frame_width * fixtures::preview_clock_frame_height),
          "reel comparison covers the full panel raster");
  for(int y = 0; y < fixtures::preview_clock_frame_height; ++y) {
   for(int x = 0; x < fixtures::preview_clock_frame_width; ++x) {
    if(x >= fixture.left && x < fixture.right && y >= fixture.top && y < fixture.bottom) continue;
    const auto index = static_cast<std::size_t>(y * fixtures::preview_clock_frame_width + x);
    require(start[index] == middle[index] && middle[index] == end[index],
            "reel movement leaves every pixel outside the clock region unchanged");
   }
  }
 }
}

void test_lifecycle_loss_cancels_clock_and_disables_input() {
 PanelSimulation app(false);
 app.load(scenario(fixtures::preview_clock_day_scenario));
 app.enable_preview_clock(true);
 app.event({{"type", "clock_step"}});
 app.invalidate_lifecycle();
 require_settled(app);
 require(all_black(app) && app.display.hit_targets().empty(), "lifecycle loss immediately removes pixels and input targets");
 const auto study_state = app.scene.study_state;
 const auto generation = app.render_generation;
 service_for(app, fixtures::preview_clock_observe_stable);
 require(app.render_generation == generation && all_black(app), "the enabled preview clock cannot redraw an invalidated display");
 app.semantic_intent("study.flip", "host_fixture");
 require(app.scene.study_state == study_state && app.scene.acknowledgement == "Action unavailable" && all_black(app),
         "lifecycle loss prevents a semantic action from changing study or restoring pixels");
 app.event({{"type", "lifecycle"}, {"state", "day"}});
 require_settled(app);
 const auto recovered_generation = app.render_generation;
 service_for(app, fixtures::preview_clock_observe_stable);
 require_settled(app);
 require(app.render_generation == recovered_generation && !all_black(app),
         "fresh lifecycle restores a settled clock without replaying its interrupted transition");
}

void test_unavailable_night_clock_stays_dark() {
 PanelSimulation app(false);
 app.load(scenario(fixtures::preview_clock_night_scenario));
 app.enable_preview_clock(true);
 app.scene.clock_source = "unavailable";
 app.render();
 service_for(app, fixtures::preview_clock_observe_stable);
 require(all_black(app) && app.display.hit_targets().empty(), "an unavailable night clock stays black without input targets");
 bool rejected = false;
 try { app.event({{"type", "clock_step"}}); }
 catch(const std::runtime_error &error) { rejected = std::string(error.what()) == "SIMULATOR.CLOCK_PREVIEW_REQUIRED"; }
 require(rejected && all_black(app), "clock-step cannot invent a fixture source for an unavailable clock");
}
}

int main() {
 try {
  test_clock_step_publishes_generations_until_settled();
  test_lifecycle_changes_cancel_motion_without_replay();
  test_frozen_reels_change_only_the_clock_pixels();
  test_lifecycle_loss_cancels_clock_and_disables_input();
  test_unavailable_night_clock_stays_dark();
  std::cout << "{\"test\":\"preview_clock_runtime\",\"passed\":true,\"evidence\":\"simulation_not_hardware_evidence\"}\n";
  return 0;
 } catch(const std::exception &error) {
  std::cerr << Json{{"test", "preview_clock_runtime"}, {"passed", false}, {"error", error.what()}}.dump() << '\n';
  return 1;
 }
}
