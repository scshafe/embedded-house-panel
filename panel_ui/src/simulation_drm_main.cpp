#include "panel_ui/simulation_drm_main.hpp"
#include "panel_ui/display_rotation.hpp"
#include "panel_ui/drm_page_flip_display.hpp"
#include "panel_ui/lifecycle_presentation_barrier.hpp"
#include "panel_ui/unix_socket_lifecycle_client.hpp"
#include "simulator/fixture_control.hpp"
#include "simulator/panel_simulation.hpp"
#include "bhp/touch_observation_protocol/unix_touch_observation_socket.h"
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <future>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstring>
#include <fcntl.h>

namespace house_panel::panel_ui {
namespace {
volatile std::sig_atomic_t stop=0;
void stopped(int){stop=1;}
void log(const bhp::simulator::Json &data){static std::mutex logging_mutex;std::lock_guard<std::mutex> guard(logging_mutex);std::cout<<bhp::simulator::Json{{"data",data},{"error",nullptr},{"meta",{{"simulation_not_hardware_evidence",true}}}}.dump()<<std::endl;}
struct Pending {bhp::panel_mode_protocol::Snapshot snapshot;std::promise<LifecycleRenderCommit> completion;};
}
int run_simulation_drm(int argc,char **argv){try{
 std::map<std::string,std::string> options;
 for(int i=1;i<argc;++i){std::string key=argv[i];if(key=="--simulation-drm")continue;if(i+1>=argc)throw std::runtime_error("SIMULATOR.ARGUMENT_REQUIRED");if(!options.emplace(key,argv[++i]).second)throw std::runtime_error("SIMULATOR.ARGUMENT_DUPLICATE");}
 for(const char *key:{"--socket-path","--supervisor-uid","--drm-device","--drm-connector-id","--touch-socket-path","--simulation-control-socket","--scenario","--touch-min-x","--touch-max-x","--touch-min-y","--touch-max-y","--display-rotation"})if(!options.count(key))throw std::runtime_error(std::string("SIMULATOR.ARGUMENT_REQUIRED:")+key);
 if(options.count("--clock-preview")&&options.at("--clock-preview")!="on"&&options.at("--clock-preview")!="off")throw std::runtime_error("SIMULATOR.CLOCK_PREVIEW_INVALID");
 const auto min_x=std::stoll(options.at("--touch-min-x")),max_x=std::stoll(options.at("--touch-max-x")),min_y=std::stoll(options.at("--touch-min-y")),max_y=std::stoll(options.at("--touch-max-y"));
 if(max_x<=min_x||max_y<=min_y)throw std::runtime_error("SIMULATOR.TOUCH_CALIBRATION_INVALID");
 const auto parsed_rotation=parse_display_rotation(options.at("--display-rotation"));if(!parsed_rotation)throw std::runtime_error("SIMULATOR.DISPLAY_ROTATION_INVALID");const auto rotation=*parsed_rotation;
 const auto uid=static_cast<std::uint32_t>(std::stoul(options.at("--supervisor-uid")));
 if(options.count("--study-config")&&options.count("--lexinet-config"))throw std::runtime_error("SIMULATOR.STUDY_CLIENT_SOURCES_EXCLUSIVE");
 bhp::simulator::PanelSimulation app(false,options.count("--state-directory")?options.at("--state-directory"):"",options.count("--lexinet-config")?options.at("--lexinet-config"):"",options.count("--launcher-config")?options.at("--launcher-config"):"");
 // The live study client (issue #21): the BHP_VOCABULARY_PANEL_* keys, production TLS only.
 if(options.count("--study-config")){auto configuration=vocabulary_panel::read_study_client_configuration(options.at("--study-config"));if(!configuration.has_value())throw std::runtime_error(configuration.error().error_code);app.attach_study_client(configuration.value());}
 app.load(bhp::simulator::read_scenario(options.at("--scenario")));app.accept_lifecycle(app.supervisor.publish("blank"));app.render();
 if(options.count("--clock-preview")&&options.at("--clock-preview")=="on")app.enable_preview_clock(true);
 // The daily forecast (ADR 0015): fixture (the recorded answers) or live; a state directory only with a source.
 if(!options.count("--weather-source")&&(options.count("--weather-state-directory")||options.count("--weather-fixture-directory")))
  throw std::runtime_error("SIMULATOR.WEATHER_SOURCE_REQUIRED");
 if(options.count("--weather-source"))
  app.attach_weather_source(options.at("--weather-source"),options.count("--weather-state-directory")?options.at("--weather-state-directory"):"",
                            options.count("--weather-fixture-directory")?options.at("--weather-fixture-directory"):"");
 // The voice-lab recorder records through the bhp-media-broker process (issue #22).
 if(options.count("--media-broker-socket")!=options.count("--media-broker-uid"))throw std::runtime_error("SIMULATOR.MEDIA_BROKER_OPTIONS_INCOMPLETE");
 if(options.count("--media-broker-socket")){app.attach_media_broker(options.at("--media-broker-socket"),static_cast<std::uint32_t>(std::stoul(options.at("--media-broker-uid"))));app.defer_level_renders(true);}
 // Kept takes go to the bhp-voice-lab-uploader process (issue #23); it drives sign-in, approval and prompts.
 if(options.count("--voice-lab-uploader-socket")!=options.count("--voice-lab-uploader-uid"))throw std::runtime_error("SIMULATOR.VOICE_LAB_UPLOADER_OPTIONS_INCOMPLETE");
 if(options.count("--voice-lab-uploader-socket"))app.attach_voice_lab_uploader(options.at("--voice-lab-uploader-socket"),static_cast<std::uint32_t>(std::stoul(options.at("--voice-lab-uploader-uid"))));
 // Music on the owner's speaker goes through the bhp-music-control process (ADR 0013); the UI holds no token.
 if(options.count("--music-control-socket")!=options.count("--music-control-uid"))throw std::runtime_error("SIMULATOR.MUSIC_CONTROL_OPTIONS_INCOMPLETE");
 // Bug reports go through the bhp-reporter process (bug-reports-and-self-repair.md); the UI never sees a report body.
 if(options.count("--bug-reporter-socket")!=options.count("--bug-reporter-uid"))throw std::runtime_error("SIMULATOR.BUG_REPORTER_OPTIONS_INCOMPLETE");
 if(options.count("--bug-reporter-socket"))app.attach_bug_reporter(options.at("--bug-reporter-socket"),static_cast<std::uint32_t>(std::stoul(options.at("--bug-reporter-uid"))));
 if(options.count("--music-control-socket"))app.attach_music_control(options.at("--music-control-socket"),static_cast<std::uint32_t>(std::stoul(options.at("--music-control-uid"))));
 DrmPageFlipDisplay drm(options.at("--drm-device"),static_cast<std::uint32_t>(std::stoul(options.at("--drm-connector-id"))));
 bhp::simulator::FixtureControl control(options.at("--simulation-control-socket"));
 std::mutex mutex;
 std::unique_ptr<Pending> pending;
 LifecyclePresentationBarrier presentation;
 std::atomic<bool> running{true};
 bool connection_failed{false};
 std::signal(SIGINT,stopped);std::signal(SIGTERM,stopped);
 std::thread lifecycle([&]{while(running&&!stop){auto connection=UnixSocketLifecycleClient::connect(options.at("--socket-path"),uid);if(!connection){std::this_thread::sleep_for(std::chrono::milliseconds(100));continue;}
  while(running&&!stop&&connection.value()->connected()){
   auto result=connection.value()->receive_render_and_acknowledge_until(std::chrono::steady_clock::now()+std::chrono::seconds(2),[&](const auto &snapshot){auto request=std::make_unique<Pending>();request->snapshot=snapshot;auto future=request->completion.get_future();{std::lock_guard<std::mutex> lock(mutex);pending=std::move(request);}while(running&&!stop){if(future.wait_for(std::chrono::milliseconds(10))==std::future_status::ready)return PanelUiResult<LifecycleRenderCommit>::success(future.get());}return PanelUiResult<LifecycleRenderCommit>::failure(PanelUiError(PanelUiErrorCode::LifecycleSocketDisconnected,"Simulation runtime stopping"));});
   if(!result){
    {std::lock_guard<std::mutex> lock(mutex);presentation.disconnect();connection_failed=true;}
    log({{"lifecycle_error",std::string(result.error().code_string())}});break;
   }
   // The canonical client returns only after sending the complete acknowledgement.
   // Completing the render promise below must never release this raster barrier.
   {std::lock_guard<std::mutex> lock(mutex);(void)presentation.complete_exchange(result.value());}
  }
 }});
 std::unique_ptr<bhp::touch_observation_protocol::UnixTouchObservationClient> touch;
 // All scene mutations and barrier state share this mutex. The worker can send an
 // acknowledgement while the main loop sleeps, but never release a different frame.
 auto commit_pixels=[&]{
  if(drm.commit(rotate_raster(app.display.pixels(),rotation)))return true;
  presentation.fail_display_commit();
  app.invalidate_lifecycle();
  running=false;
  log({{"error_code","SIMULATOR.DRM_COMMIT_FAILED"},{"presentation_result","failed"}});
  return false;
 };
 auto finish_lifecycle=[&]{
  running=false;
  {std::lock_guard<std::mutex> lock(mutex);presentation.stop();}
  lifecycle.join();
 };
 try{while(running&&!stop&&!app.quit){
  {
   std::lock_guard<std::mutex> lock(mutex);
   auto request=std::move(pending);
   if(connection_failed){
    connection_failed=false;
    app.invalidate_lifecycle();
    if(!commit_pixels())throw std::runtime_error("SIMULATOR.DRM_COMMIT_FAILED");
   }
   if(request){
    app.accept_lifecycle(request->snapshot);
    const auto generation=app.render();
    if(!presentation.hold_until_exchange_completed(generation))throw std::runtime_error("SIMULATOR.LIFECYCLE_PRESENTATION_BARRIER_INVALID");
    const bool display_committed=commit_pixels();
    const bool committed=display_committed&&!app.scene.presentation_failure;
    request->completion.set_value({generation,committed?bhp::panel_mode_protocol::PresentationResult::Committed:bhp::panel_mode_protocol::PresentationResult::Failed});
    auto status=app.status();
    status["presentation_result"]=committed?"committed":"failed";
    status["display_commit_backend"]="drm_page_flip_completion";
    status["snapshot_id"]=request->snapshot.snapshot_id;
    log(status);
    if(!display_committed)throw std::runtime_error("SIMULATOR.DRM_COMMIT_FAILED");
   }
   if(presentation.scene_changes_allowed()){
    if(!touch){auto connected=bhp::touch_observation_protocol::UnixTouchObservationClient::connect(options.at("--touch-socket-path"),uid);if(connected)touch=std::move(connected).value();}
    if(touch){auto result=touch->try_receive();if(!result)touch.reset();else if(result.value()){const auto &record=*result.value();const auto &binding=record.presentation_binding;const auto &snapshot=app.scene.lifecycle;
      if(record.contact_state==bhp::touch_observation_protocol::ContactState::Completed&&binding.supervisor_session_id==snapshot.supervisor_session_id&&binding.lifecycle_snapshot_id==snapshot.snapshot_id&&binding.lifecycle_event_sequence==snapshot.event_sequence&&binding.ui_connection_challenge==snapshot.connection_challenge&&binding.committed_render_generation==app.render_generation&&presentation.input_ready(app.render_generation)&&record.raw_axis_x>=min_x&&record.raw_axis_x<=max_x&&record.raw_axis_y>=min_y&&record.raw_axis_y<=max_y){
       const auto x=(record.raw_axis_x-min_x)*1023/(max_x-min_x);const auto y=(record.raw_axis_y-min_y)*599/(max_y-min_y);if(x>=0&&x<=1023&&y>=0&&y<=599){const auto logical=logical_touch_point(static_cast<int>(x),static_cast<int>(y),1024,600,rotation);app.tap(logical.first,logical.second,{record.touch_observation_id,binding});if(!commit_pixels())throw std::runtime_error("SIMULATOR.DRM_COMMIT_FAILED");log({{"touch_commit",true},{"status",app.status()}});}}
     }}
    // Privileged fixture selection can recover a sent Failed presentation.
    // It is not touch input, but still cannot cross an in-flight raster hold.
    // A malformed or failing fixture request is refused and logged; it never
    // ends the UI (review Q7). Only a failed display commit still does.
    if(presentation.exchange_completed_generation()!=0){
     auto frame=control.receive();
     if(!frame.rejected.empty())log({{"control_rejected",frame.rejected}});
     else if(frame.event){
      const auto &event=*frame.event;
      const auto type=event.at("type").get<std::string>();
      bool redraw=false;
      try{
       if(type=="scenario"){redraw=true;app.load(bhp::simulator::read_scenario(event.at("path").get<std::string>()),false,true);}
       else if(type=="clock_preview"||type=="clock_step"){redraw=true;app.event(event);}
       else if(type=="screenshot")app.display.write_png(event.at("path").get<std::string>());
       else if(type=="quit")app.quit=true;
      }catch(const std::exception &error){
       const std::string what=error.what();
       log({{"control_rejected",what.rfind("SIMULATOR.",0)==0||what.rfind("PANEL_UI.",0)==0?what.substr(0,what.find(':')):"SIMULATOR.CONTROL_REQUEST_FAILED"}});
      }
      if(redraw&&!commit_pixels())throw std::runtime_error("SIMULATOR.DRM_COMMIT_FAILED");
      log(app.status());
     }
    }
    const auto before_service=app.render_generation;app.service();if(before_service!=app.render_generation){if(!commit_pixels())throw std::runtime_error("SIMULATOR.DRM_COMMIT_FAILED");log({{"async_commit",true},{"status",app.status()}});}
   }
  }
  // Keep the raster hold interruptible; no unbounded wait is introduced here.
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
 }}catch(...){finish_lifecycle();throw;}
 finish_lifecycle();return 0;
}catch(const std::exception &error){log({{"error_code","SIMULATOR.DRM_RUNTIME_FAILED"},{"error_message",error.what()}});return 1;}}
}
