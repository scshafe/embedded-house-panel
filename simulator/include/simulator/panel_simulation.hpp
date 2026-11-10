#pragma once
#include "vocabulary_panel/study_client_configuration.h"
#include "panel_ui/panel_scene.hpp"
#include "panel_ui/lvgl_panel_display.hpp"
#include "panel_ui/shell_controller.hpp"
#include "vocabulary_panel/flash_card_session.h"
#include <nlohmann/json.hpp>
#include <deque>
#include <chrono>
#include <cstdint>
#include <memory>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include "simulator/fake_voice_connection.hpp"
#include "simulator/preview_clock.hpp"
#include "simulator/reconnect_schedule.hpp"
#include "simulator/voice_lab_runtime.hpp"
#include "music_control/music_protocol.h"
#include "bug_reporter/reporter_protocol.h"
#include "bhp/structured_logger.h"
#include <iostream>

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
 explicit PanelSimulation(bool window,std::string state_directory={},std::string lexinet_configuration_path={},std::string application_catalog_path={});
 ~PanelSimulation();
 /**
  * Live study client from the BHP_VOCABULARY_PANEL_* configuration, instead of a
  * --lexinet-config JSON. Needs a durable state directory and no other client.
  * test_ca_pem is for the renderer's fixture-server tests only.
  */
 void attach_study_client(const vocabulary_panel::StudyClientConfiguration &configuration,const std::string &test_ca_pem={});
 void load(const Json &scenario,bool update_lifecycle=true,bool fixture_override=false);
 void accept_lifecycle(const bhp::panel_mode_protocol::Snapshot &snapshot);
 /** Cancels dependent work and latches black until a fresh authenticated snapshot arrives. */
 void invalidate_lifecycle();
 void event(const Json &event);
 void tap(int x,int y);
 /** A tap the supervisor observed; voice_lab.record and .retake forward its evidence to the broker. */
 void tap(int x,int y,const VoiceLabRuntime::TouchEvidence &evidence);
 /** Records through the bhp-media-broker process instead of the in-process fake (the board). */
 void attach_media_broker(const std::string &socket_path,std::uint32_t broker_uid);
 /** The voice-lab uploader's socket (exact UID): the recorder's sign-in, approval, prompts and outbox. */
 void attach_voice_lab_uploader(const std::string &socket_path,std::uint32_t uploader_uid);
 /** Test seam: an already-connected uploader peer (its UID is still checked). */
 void attach_voice_lab_uploader_peer(int descriptor,std::uint32_t uploader_uid);
 /**
  * The music-control service's socket (exact UID): the music remote's state and commands (ADR 0013).
  * Never fatal: when the service is down, or lost later, the screen shows unavailable and the
  * UI connects again on a ReconnectSchedule (1 s doubling to 60 s; review Q7).
  */
 void attach_music_control(const std::string &socket_path,std::uint32_t service_uid);
 /** Test seam: an already-connected music-control peer (its UID is still checked). */
 void attach_music_control_peer(int descriptor,std::uint32_t service_uid);
 /** The bug reporter's socket (exact UID): the report screen's snapshots, submits and list. Never fatal, as for music. */
 void attach_bug_reporter(const std::string &socket_path,std::uint32_t reporter_uid);
 /** Test seam: the clock both reconnect schedules read. */
 void set_reconnect_clock(ReconnectSchedule::Clock now);
 /** Test seam: an already-connected reporter peer (its UID is still checked). */
 void attach_bug_reporter_peer(int descriptor,std::uint32_t reporter_uid);
 /**
  * Level-only recorder updates wait for the next lifecycle render instead of drawing
  * at once, so a live meter cannot keep touches from matching the committed frame.
  */
 void defer_level_renders(bool defer){defer_level_renders_=defer;}
 void semantic_intent(const std::string &id,const std::string &source);
 std::uint64_t render();
 void service();
 /** Where the in-process broker keeps takes (a temporary directory). */
 std::string voice_lab_take_root() const {return voice_lab_runtime_?voice_lab_runtime_->take_root():std::string();}
 std::string voice_lab_handoff_root() const {return voice_lab_runtime_?voice_lab_runtime_->handoff_root():std::string();}
 std::string voice_lab_report_handoff_root() const {return voice_lab_runtime_?voice_lab_runtime_->report_handoff_root():std::string();}
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
 void pump_voice_lab();
 void pump_clock();
 void cancel_clock_animation();
 void utterance(const std::string &phrase);
 void route_intent(const house_panel::panel_ui::InteractionIntentEnvelope &intent);
 void voice_lab_intent(const std::string &id);
 void project_uploader(const voice_lab_recorder::uploader_protocol::State &state);
 void pump_music();
 void music_intent(const std::string &id);
 void project_music(const music_control::protocol::State &state);
 bool attach_music_client(std::unique_ptr<music_control::protocol::MusicClient> client);
 struct ServiceEndpoint {std::string path;std::uint32_t uid;};
 /** Where the music service listens, once configured; its reconnect schedule. */
 std::optional<ServiceEndpoint> music_endpoint_;
 ReconnectSchedule music_retry_;
 bool connect_music();
 /** Drops the connection, shows unavailable and schedules the next try. */
 void music_lost();
 /** The music-control connection and the last state it sent, while one is attached. */
 std::unique_ptr<music_control::protocol::MusicClient> music_;
 void open_report();
 void report_intent(const std::string &id);
 void pump_reporter();
 bool attach_reporter_client(std::unique_ptr<bug_reporter::protocol::ReporterClient> client);
 std::optional<ServiceEndpoint> reporter_endpoint_;
 ReconnectSchedule reporter_retry_;
 bool connect_reporter();
 void reporter_lost();
 void log_service(const std::string &message,const std::string &socket_path);
 /** The reporter connection, the snapshot of the tap that opened the report screen, and where Done returns. */
 std::unique_ptr<bug_reporter::protocol::ReporterClient> reporter_;
 std::string report_snapshot_;
 std::string report_return_surface_{"home"};
 /** Who started the broker's current take: the voice-lab recorder or the report screen (step 3). */
 bool report_capture_{false};
 /** The spoken description the broker handed to the reporter, for Send. */
 std::string report_take_;
 /** Handles a broker event for a report's spoken description; false when the take is the recorder's. */
 bool report_capture_event(const VoiceLabRuntime::Event &event);
 std::optional<music_control::protocol::State> music_state_;
 /** The last state the uploader sent, while one is attached. */
 std::optional<voice_lab_recorder::uploader_protocol::State> uploader_state_;
 /** Kept takes waiting for the broker's hand-off: take id to (prompt id, requested at). */
 std::map<std::string,std::pair<std::string,std::string>> pending_keeps_;
 /** When the current prompt was put on the screen (RFC 3339 UTC). */
 std::string prompt_requested_at_;
 house_panel::panel_ui::ApplicationCatalog application_catalog_;
 std::unique_ptr<FakeVoiceConnection> voice_;
 std::unique_ptr<VoiceLabRuntime> voice_lab_runtime_;
 std::optional<VoiceLabRuntime::TouchEvidence> pending_touch_evidence_;
 bool defer_level_renders_{false};
 /** The last text fit logged, so a card that stays on the screen is logged once. */
 house_panel::panel_ui::LvglPanelDisplay::TextFit last_text_fit_;
 bhp::StructuredLogger text_log_{"panel_ui",std::cerr};
 int voice_lab_take_peak_{-1200};
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
