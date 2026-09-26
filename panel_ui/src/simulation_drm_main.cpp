#include "panel_ui/simulation_drm_main.hpp"
#include "panel_ui/drm_page_flip_display.hpp"
#include "panel_ui/unix_socket_lifecycle_client.hpp"
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
class FixtureControl final {
public:
 explicit FixtureControl(std::string path):path_(std::move(path)){
  if(path_.empty()||path_.front()!='/'||path_.size()>=sizeof(sockaddr_un::sun_path))throw std::runtime_error("SIMULATOR.CONTROL_PATH_INVALID");
  struct stat s{};if(lstat(path_.c_str(),&s)==0)throw std::runtime_error("SIMULATOR.CONTROL_PATH_EXISTS");
  fd_=socket(AF_UNIX,SOCK_DGRAM|SOCK_CLOEXEC|SOCK_NONBLOCK,0);if(fd_<0)throw std::runtime_error("SIMULATOR.CONTROL_SOCKET_FAILED");
  sockaddr_un address{};address.sun_family=AF_UNIX;std::memcpy(address.sun_path,path_.c_str(),path_.size()+1);
  if(bind(fd_,reinterpret_cast<sockaddr*>(&address),sizeof address)<0||chmod(path_.c_str(),0600)<0)throw std::runtime_error("SIMULATOR.CONTROL_BIND_FAILED");
 }
 ~FixtureControl(){if(fd_>=0)close(fd_);unlink(path_.c_str());}
 std::optional<bhp::simulator::Json> receive(){char bytes[65536];auto size=recv(fd_,bytes,sizeof bytes,MSG_DONTWAIT);if(size<0&&(errno==EAGAIN||errno==EWOULDBLOCK))return std::nullopt;if(size<=0||size==static_cast<ssize_t>(sizeof bytes))throw std::runtime_error("SIMULATOR.CONTROL_FRAME_INVALID");return bhp::simulator::Json::parse(bytes,bytes+size);}
private:int fd_{-1};std::string path_;
};
}
int run_simulation_drm(int argc,char **argv){try{
 std::map<std::string,std::string> options;
 for(int i=1;i<argc;++i){std::string key=argv[i];if(key=="--simulation-drm")continue;if(i+1>=argc)throw std::runtime_error("SIMULATOR.ARGUMENT_REQUIRED");if(!options.emplace(key,argv[++i]).second)throw std::runtime_error("SIMULATOR.ARGUMENT_DUPLICATE");}
 for(const char *key:{"--socket-path","--supervisor-uid","--drm-device","--drm-connector-id","--touch-socket-path","--simulation-control-socket","--scenario","--touch-min-x","--touch-max-x","--touch-min-y","--touch-max-y"})if(!options.count(key))throw std::runtime_error(std::string("SIMULATOR.ARGUMENT_REQUIRED:")+key);
 const auto min_x=std::stoll(options.at("--touch-min-x")),max_x=std::stoll(options.at("--touch-max-x")),min_y=std::stoll(options.at("--touch-min-y")),max_y=std::stoll(options.at("--touch-max-y"));
 if(max_x<=min_x||max_y<=min_y)throw std::runtime_error("SIMULATOR.TOUCH_CALIBRATION_INVALID");
 const auto uid=static_cast<std::uint32_t>(std::stoul(options.at("--supervisor-uid")));
 bhp::simulator::PanelSimulation app(false,options.count("--state-directory")?options.at("--state-directory"):"",options.count("--lexinet-config")?options.at("--lexinet-config"):"");
 app.load(bhp::simulator::read_scenario(options.at("--scenario")));app.accept_lifecycle(app.supervisor.publish("blank"));app.render();
 DrmPageFlipDisplay drm(options.at("--drm-device"),static_cast<std::uint32_t>(std::stoul(options.at("--drm-connector-id"))));
 FixtureControl control(options.at("--simulation-control-socket"));
 std::mutex mutex;std::unique_ptr<Pending> pending;std::atomic<bool> running{true},connection_failed{false};std::atomic<std::uint64_t> acknowledged_generation{0};
 std::signal(SIGINT,stopped);std::signal(SIGTERM,stopped);
 std::thread lifecycle([&]{while(running&&!stop){auto connection=UnixSocketLifecycleClient::connect(options.at("--socket-path"),uid);if(!connection){std::this_thread::sleep_for(std::chrono::milliseconds(100));continue;}
  while(running&&!stop&&connection.value()->connected()){
   auto result=connection.value()->receive_render_and_acknowledge_until(std::chrono::steady_clock::now()+std::chrono::seconds(2),[&](const auto &snapshot){auto request=std::make_unique<Pending>();request->snapshot=snapshot;auto future=request->completion.get_future();{std::lock_guard<std::mutex> lock(mutex);pending=std::move(request);}while(running&&!stop){if(future.wait_for(std::chrono::milliseconds(10))==std::future_status::ready)return PanelUiResult<LifecycleRenderCommit>::success(future.get());}return PanelUiResult<LifecycleRenderCommit>::failure(PanelUiError(PanelUiErrorCode::LifecycleSocketDisconnected,"Simulation runtime stopping"));});
   if(!result){acknowledged_generation=0;connection_failed=true;log({{"lifecycle_error",std::string(result.error().code_string())}});break;}
   acknowledged_generation=result.value().render_generation;
  }
 }});
 std::unique_ptr<bhp::touch_observation_protocol::UnixTouchObservationClient> touch;
 try{while(!stop&&!app.quit){
  std::unique_ptr<Pending> request;{std::lock_guard<std::mutex> lock(mutex);request=std::move(pending);}
  if(connection_failed.exchange(false)){app.invalidate_lifecycle();drm.commit(app.display.pixels());}
  if(request){acknowledged_generation=0;app.accept_lifecycle(request->snapshot);auto generation=app.render();bool committed=drm.commit(app.display.pixels())&&!app.scene.presentation_failure;request->completion.set_value({generation,committed?bhp::panel_mode_protocol::PresentationResult::Committed:bhp::panel_mode_protocol::PresentationResult::Failed});auto status=app.status();status["presentation_result"]=committed?"committed":"failed";status["display_commit_backend"]="drm_page_flip_completion";status["snapshot_id"]=request->snapshot.snapshot_id;log(status);}
  if(!touch){auto connected=bhp::touch_observation_protocol::UnixTouchObservationClient::connect(options.at("--touch-socket-path"),uid);if(connected)touch=std::move(connected).value();}
  if(touch){auto result=touch->try_receive();if(!result)touch.reset();else if(result.value()){const auto &record=*result.value();const auto &binding=record.presentation_binding;const auto &snapshot=app.scene.lifecycle;
    if(record.contact_state==bhp::touch_observation_protocol::ContactState::Completed&&binding.supervisor_session_id==snapshot.supervisor_session_id&&binding.lifecycle_snapshot_id==snapshot.snapshot_id&&binding.lifecycle_event_sequence==snapshot.event_sequence&&binding.ui_connection_challenge==snapshot.connection_challenge&&binding.committed_render_generation==app.render_generation&&acknowledged_generation==app.render_generation&&record.raw_axis_x>=min_x&&record.raw_axis_x<=max_x&&record.raw_axis_y>=min_y&&record.raw_axis_y<=max_y){
     const auto x=(record.raw_axis_x-min_x)*1023/(max_x-min_x);const auto y=(record.raw_axis_y-min_y)*599/(max_y-min_y);if(x>=0&&x<=1023&&y>=0&&y<=599){app.tap(static_cast<int>(x),static_cast<int>(y));bool committed=drm.commit(app.display.pixels());log({{"touch_commit",committed},{"status",app.status()}});}}
   }}
  if(acknowledged_generation==app.render_generation)if(auto event=control.receive()){const auto type=event->at("type").get<std::string>();if(type=="scenario"){app.load(bhp::simulator::read_scenario(event->at("path")),false,true);drm.commit(app.display.pixels());}else if(type=="screenshot")app.display.write_png(event->at("path"));else if(type=="quit")app.quit=true;else log({{"control_rejected","SIMULATOR.CONTROL_LIFECYCLE_OR_INPUT_FORBIDDEN"}});log(app.status());}
  const auto before_service=app.render_generation;app.service();if(before_service!=app.render_generation){bool committed=drm.commit(app.display.pixels());log({{"async_commit",committed},{"status",app.status()}});}
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
 }}catch(...){running=false;stop=1;lifecycle.join();throw;}
 running=false;stop=1;lifecycle.join();return 0;
}catch(const std::exception &error){log({{"error_code","SIMULATOR.DRM_RUNTIME_FAILED"},{"error_message",error.what()}});return 1;}}
}
