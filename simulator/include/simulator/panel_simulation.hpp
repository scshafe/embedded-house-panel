#pragma once
#include "panel_ui/panel_scene.hpp"
#include "panel_ui/lvgl_panel_display.hpp"
#include "panel_ui/shell_controller.hpp"
#include "vocabulary_panel/flash_card_session.h"
#include <nlohmann/json.hpp>
#include <deque>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include "simulator/fake_voice_connection.hpp"
#include "simulator/preview_clock.hpp"

namespace bhp::simulator {
using Json=nlohmann::json;
/** Parses a bounded, closed scenario-v1 file. All values are fixture-only. */
Json read_scenario(const std::string &path);
/** @canonical scripted_panel_supervisor -- native-only lifecycle and raw-pointer observer.
 * Uses canonical protocol-v2 types. Cannot issue redeemable production provenance.
 */
class ScriptedPanelSupervisor final {
public:
 bhp::panel_mode_protocol::Snapshot publish(const std::string &state);
 void observe_pointer(bool down,int x,int y);
 std::optional<std::pair<int,int>> take_contact();
private:
 std::uint64_t sequence_{0}; bool contact_active_{false};
 std::deque<std::pair<int,int>> contacts_;
};
class SimulationMonotonicClock final:public house_panel::panel_ui::MonotonicClock {
public:
 std::chrono::steady_clock::time_point now() const noexcept override {return std::chrono::steady_clock::time_point(std::chrono::milliseconds(ticks_));}
 void advance(){ticks_+=1000;}
private:std::uint64_t ticks_{1000};
};
/** Same fixture shell assembly for memory, SDL and explicitly selected guest simulation. */
class PanelSimulation final {
public:
 explicit PanelSimulation(bool window,std::string state_directory={},std::string lexinet_configuration_path={});
 ~PanelSimulation();
 void load(const Json &scenario,bool update_lifecycle=true,bool fixture_override=false);
 void accept_lifecycle(const bhp::panel_mode_protocol::Snapshot &snapshot);
 /** Cancels dependent work and latches black until a fresh authenticated snapshot arrives. */
 void invalidate_lifecycle();
 void event(const Json &event);
 void tap(int x,int y);
 void semantic_intent(const std::string &id,const std::string &source);
 std::uint64_t render();
 void service();
 /** Explicit simulation clock; advances fixture minutes, never household civil time. */
 void enable_preview_clock(bool enabled);
 void advance_preview_clock();
 house_panel::panel_ui::LvglPanelDisplay display;
 house_panel::panel_ui::PanelScene scene;
 ScriptedPanelSupervisor supervisor;
 bool quit{false};
 std::uint64_t render_generation{0};
 Json status() const;
private:
 /** Routes an accepted app change without resetting the study session or outbox. */
 void switch_surface(const std::string &surface_id);
 /** Closes the fake recognition lease and clears the shell's active voice binding. */
 void close_voice_context();
 void project_study();
 void seed_study();
 void pump_study();
 void pump_voice();
 void pump_clock();
 void cancel_clock_animation();
 void utterance(const std::string &phrase);
 void route_intent(const house_panel::panel_ui::InteractionIntentEnvelope &intent);
 std::unique_ptr<FakeVoiceConnection> voice_;
 bool voice_pending_{false};
 std::uint64_t utterance_sequence_{0};
 struct StudyNetwork;
 std::unique_ptr<StudyNetwork> network_;
 bool fixture_override_{false};
 bool lifecycle_available_{true};
 std::shared_ptr<vocabulary_panel::StudyStore> persistent_store_;
 SimulationMonotonicClock clock_;
 PreviewClock preview_clock_;
 bool preview_clock_enabled_{false},clock_animation_live_{false};
 std::uint64_t last_clock_sample_ms_{0};
 std::unique_ptr<house_panel::panel_ui::ShellController> controller_;
 std::unique_ptr<vocabulary_panel::FlashCardSession> study_;
 std::shared_ptr<vocabulary_panel::StudyStore> store_;
 std::uint64_t source_sequence_{0};
 std::uint64_t context_sequence_{0};
 std::string context_nonce_{"simulation-context"};
};
}
