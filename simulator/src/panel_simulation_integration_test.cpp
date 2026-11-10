#include "weather_panel/weather_client.h"
#include "weather_panel/weather_store.h"
#include "weather_panel/weather_worker.h"
#include <atomic>
#include "simulator/panel_simulation.hpp"
#include "vocabulary_panel/study_wire_codec.h"
#include "bhp/media_window_protocol/media_window_protocol.h"
#include "voice_lab_recorder/uploader_protocol.h"
#include "music_control/music_protocol.h"
#include "bug_reporter/reporter_protocol.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <cstring>
#include <unistd.h>
#include <algorithm>
#include <fstream>
#include <iterator>
#include <iostream>
#include <chrono>
#include <stdexcept>
#include <thread>
#include <filesystem>
#include <functional>
using bhp::simulator::PanelSimulation;
void require(bool condition,const char *message){if(!condition)throw std::runtime_error(message);}
std::string read_bytes(const std::filesystem::path &path){
 std::ifstream input(path,std::ios::binary);require(input.good(),"test state file must exist");
 return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
}
void test_voice_lab_records_through_the_broker(const std::filesystem::path &root){
 PanelSimulation app(false);
 app.load(bhp::simulator::read_scenario((root/"simulator/scenarios/home.json").string()));
 app.semantic_intent("shell.open_voice_lab","touch");
 require(app.scene.surface_id=="voice_lab"&&app.scene.voice_lab.state=="ready","the recorder opens from home");
 const auto has_target=[&app](const std::string &intent){
  for(const auto &target:app.display.hit_targets())if(target.intent_id==intent)return true;
  return false;
 };
 const auto until=[&app](const std::function<bool()> &done,const char *message){
  for(int i=0;i<300&&!done();++i){app.service();std::this_thread::sleep_for(std::chrono::milliseconds(5));}
  require(done(),message);
 };
 const auto takes=[&app]{
  std::size_t count=0;
  for(const auto &entry:std::filesystem::directory_iterator(app.voice_lab_take_root()))
   count+=entry.path().filename().string().rfind("take-",0)==0;
  return count;
 };
 require(has_target("voice_lab.record")&&has_target("voice_lab.skip")&&!has_target("voice_lab.keep"),"ready offers record and skip");
 app.semantic_intent("voice_lab.keep","touch");
 require(app.scene.acknowledgement=="Action unavailable","keep needs a take");
 app.semantic_intent("voice_lab.record","voice");
 require(app.scene.voice_lab.state=="ready","voice cannot start a take");
 app.semantic_intent("voice_lab.record","touch");
 until([&]{return app.scene.voice_lab.state=="recording";},"a touch arms a lease and the broker starts the take");
 require(has_target("voice_lab.stop"),"recording offers stop");
 until([&]{return app.scene.voice_lab.elapsed_ms>=200;},"level events advance the elapsed time");
 require(app.scene.voice_lab.level_tenths.size()==6&&app.scene.voice_lab.level_tenths[0]>-1200,"six live channel levels");
 app.semantic_intent("voice_lab.stop","touch");
 until([&]{return app.scene.voice_lab.state=="review";},"stop commits the take for review");
 require(!app.scene.voice_lab.take_id.empty()&&takes()==1&&app.scene.voice_lab.take_ms>0,"the committed take is on disk");
 require(std::filesystem::exists(app.voice_lab_take_root()+"/"+app.scene.voice_lab.take_id+"/capture.json"),"with its capture record");
 const auto queued=app.scene.voice_lab.queued_takes;const auto prompt=app.scene.voice_lab.prompt_number;
 const auto kept_id=app.scene.voice_lab.take_id;
 app.semantic_intent("voice_lab.keep","touch");
 require(app.scene.voice_lab.state=="ready"&&app.scene.voice_lab.queued_takes==queued+1&&app.scene.voice_lab.prompt_number==prompt+1,"keep keeps the take and moves on");
 until([&]{return takes()==0;},"the broker hands the kept take over");
 require(std::filesystem::exists(app.voice_lab_handoff_root()+"/"+kept_id+"/original.wav")&&std::filesystem::exists(app.voice_lab_handoff_root()+"/"+kept_id+"/capture.json"),"the kept take is in the uploader's hand-off directory");
 app.semantic_intent("voice_lab.record","touch");
 until([&]{return app.scene.voice_lab.state=="recording";},"a second take starts");
 app.semantic_intent("voice_lab.stop","touch");
 until([&]{return app.scene.voice_lab.state=="review";},"the second take commits");
 app.semantic_intent("voice_lab.reject","touch");
 until([&]{return takes()==0;},"reject deletes the take through the broker");
 require(app.scene.voice_lab.state=="ready"&&app.scene.voice_lab.queued_takes==queued+1,"a rejected take is not queued");
 app.semantic_intent("voice_lab.record","touch");
 until([&]{return app.scene.voice_lab.state=="recording";},"a third take starts");
 app.event({{"type","lifecycle"},{"state","grace"}});
 until([&]{return app.scene.voice_lab.state=="ready";},"leaving day-present closes the window and fails the take");
 require(app.scene.acknowledgement=="Recording stopped"&&takes()==0,"a failed take leaves no files");
 app.event({{"type","lifecycle"},{"state","day"}});
 app.semantic_intent("shell.home","touch");
 require(app.scene.surface_id=="home","home leaves the recorder");
}
// The recorder on the voice-lab uploader's protocol, with this test as the
// uploader on the other end of a socket pair (the real uploader is tested in
// voice_lab_recorder and live against voice-lab's server).
void test_voice_lab_follows_the_uploader(const std::filesystem::path &root){
 namespace wire=voice_lab_recorder::uploader_protocol;
 PanelSimulation app(false);
 app.load(bhp::simulator::read_scenario((root/"simulator/scenarios/home.json").string()));
 app.semantic_intent("shell.open_voice_lab","touch");
 int pair[2];
 require(::socketpair(AF_UNIX,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0,pair)==0,"socket pair");
 app.attach_voice_lab_uploader_peer(pair[0],::getuid());
 bhp::media_window_protocol::FrameChannel uploader(pair[1],wire::kMaximumFrameBytes);
 const auto until=[&app](const std::function<bool()> &done,const char *message){
  for(int i=0;i<400&&!done();++i){app.service();std::this_thread::sleep_for(std::chrono::milliseconds(5));}
  require(done(),message);
 };
 const auto next_request=[&]()->wire::UiRequest{
  for(int i=0;i<400;++i){
   app.service();
   auto frame=uploader.receive();
   require(frame.has_value(),"the UI keeps the uploader connection");
   if(frame.value()){auto request=wire::decode_ui_request(*frame.value());require(request.has_value(),"a valid request");return request.value();}
   std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  throw std::runtime_error("no request reached the uploader");
 };
 std::uint64_t sequence=0;
 const auto send=[&](wire::State state){state.sequence=++sequence;require(uploader.send(wire::encode(wire::UploaderEvent(state)).value()).has_value(),"state sent");};
 require(std::holds_alternative<wire::StatusRequest>(next_request()),"the UI asks for the state when it attaches");
 wire::State state;
 send(state);
 until([&]{return app.scene.voice_lab.state=="signed_out";},"signed out shows Sign in");
 app.semantic_intent("voice_lab.sign_in","touch");
 require(std::holds_alternative<wire::SignInRequest>(next_request()),"Sign in asks the uploader for a code");
 state.sign_in={"pending","https://id.example.ts.net/device","WXYZ-1234",900};
 send(state);
 until([&]{return app.scene.voice_lab.state=="signing_in"&&app.scene.voice_lab.user_code=="WXYZ-1234";},"the code is shown");
 require(app.scene.voice_lab.verification_uri=="id.example.ts.net/device","the plain URI, never a complete link");
 state.sign_in={"signed_in","","",0};
 state.approval={"preview","voice-lab.example.ts.net","nPC1","2027-10-20T12:00:00Z",std::string(64,'d')};
 send(state);
 until([&]{return app.scene.voice_lab.state=="approve";},"signed in, the PC is approved first");
 require(app.scene.voice_lab.approval_node=="nPC1"&&app.scene.voice_lab.approval_expires=="2027-10-20","the preview is shown");
 app.semantic_intent("voice_lab.approve","touch");
 const auto approve=next_request();
 require(std::holds_alternative<wire::ApproveRequest>(approve)&&std::get<wire::ApproveRequest>(approve).approval_sha256==std::string(64,'d'),"Approve names the reviewed digest");
 state.approval={"in_force","voice-lab.example.ts.net","nPC1","2027-10-20T12:00:00Z",std::string(64,'d')};
 state.catalog_version=1;
 state.prompts={{"room-tone-01","","room-tone","missing"},{"reference-neutral-01","First prompt.","reference","missing"},{"reference-warm-01","Second prompt.","reference","missing"}};
 send(state);
 until([&]{return app.scene.voice_lab.state=="ready";},"approved, the recorder is ready");
 require(app.scene.voice_lab.prompt_id=="reference-neutral-01"&&app.scene.voice_lab.prompt_text=="First prompt."&&app.scene.voice_lab.missing==2,"Continue Missing skips silent prompts");
 app.semantic_intent("voice_lab.record","touch");
 until([&]{return app.scene.voice_lab.state=="recording";},"recording");
 app.semantic_intent("voice_lab.stop","touch");
 until([&]{return app.scene.voice_lab.state=="review";},"review");
 const auto take=app.scene.voice_lab.take_id;
 app.semantic_intent("voice_lab.keep","touch");
 const auto keep=next_request();
 require(std::holds_alternative<wire::KeepRequest>(keep),"after the hand-off the uploader is asked to keep the take");
 const auto &kept=std::get<wire::KeepRequest>(keep);
 require(kept.take_id==take&&kept.prompt_id=="reference-neutral-01"&&kept.requested_at.size()==20,"for this prompt, with when it was shown");
 require(std::filesystem::exists(app.voice_lab_handoff_root()+"/"+take+"/original.wav"),"the uploader finds the take in the hand-off directory");
 state.queued=1;state.prompts[1].status="draft";state.blocked="VOICE_LAB.TAILNET_UNAVAILABLE";
 send(state);
 until([&]{return app.scene.voice_lab.notice=="Takes wait: the PC is not reachable";},"why uploads wait, in words");
 require(app.scene.voice_lab.prompt_id=="reference-warm-01"&&app.scene.voice_lab.queued_takes==1&&app.scene.voice_lab.missing==1,"the next missing prompt, one take queued");
 ::close(pair[1]);
 until([&]{return app.scene.voice_lab.notice=="The uploader is not running";},"a lost uploader is shown");
}
// The music surface on the music-control protocol, with this test as the
// service on the other end of a socket pair (the real service is tested in
// music_control against scripted Spotify answers).
void test_music_follows_the_service(const std::filesystem::path &root){
 namespace wire=music_control::protocol;
 PanelSimulation app(false);
 app.load(bhp::simulator::read_scenario((root/"simulator/scenarios/home.json").string()));
 app.semantic_intent("shell.open_music","touch");
 require(app.scene.surface_id=="music","the music surface opens from home");
 app.semantic_intent("music.use_speaker","touch");
 require(app.scene.acknowledgement=="Music is not running","without the service nothing is sent");
 int pair[2];
 require(::socketpair(AF_UNIX,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0,pair)==0,"socket pair");
 app.attach_music_control_peer(pair[0],::getuid());
 require(app.scene.music.state=="checking","attached, the surface waits for the service");
 bhp::media_window_protocol::FrameChannel service(pair[1],wire::kMaximumFrameBytes);
 const auto until=[&app](const std::function<bool()> &done,const char *message){
  for(int i=0;i<400&&!done();++i){app.service();std::this_thread::sleep_for(std::chrono::milliseconds(5));}
  require(done(),message);
 };
 const auto next_request=[&]()->wire::UiRequest{
  for(int i=0;i<400;++i){
   app.service();
   auto frame=service.receive();
   require(frame.has_value(),"the UI keeps the music-control connection");
   if(frame.value()){auto request=wire::decode_ui_request(*frame.value());require(request.has_value(),"a valid request");return request.value();}
   std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  throw std::runtime_error("no request reached the music service");
 };
 const auto has_target=[&app](const std::string &intent){
  for(const auto &target:app.display.hit_targets())if(target.intent_id==intent)return true;
  return false;
 };
 std::uint64_t sequence=0;
 const auto send=[&](wire::MusicEvent event){
  if(auto *state=std::get_if<wire::State>(&event))state->sequence=++sequence;
  require(service.send(wire::encode(event).value()).has_value(),"event sent");
 };
 require(std::holds_alternative<wire::StatusRequest>(next_request()),"the UI asks for the state when it attaches");
 wire::State state;
 state.output.name="Living Room Bose";
 send(state);
 until([&]{return app.scene.music.state=="signed_out";},"no token shows the PC sign-in");
 require(!has_target("music.use_speaker"),"signed out, there is nothing to press");
 state.sign_in="signed_in";state.output={"available","Living Room Bose",true,35u};
 send(state);
 until([&]{return app.scene.music.state=="ready"&&app.scene.music.volume_percent==35;},"the speaker is ready");
 require(has_target("music.use_speaker"),"Play here is on the screen");
 app.semantic_intent("music.use_speaker","touch");
 require(std::holds_alternative<wire::SelectOutputRequest>(next_request()),"Play here moves playback to the speaker");
 state.output.state="active";state.playback=wire::Playback{true,"Slow Sunrise","The Fixture Band"};
 send(state);
 until([&]{return app.scene.music.state=="playing"&&app.scene.music.title=="Slow Sunrise";},"playing shows the track");
 require(has_target("music.pause")&&has_target("music.next")&&has_target("music.volume_up"),"pause, skip and volume are on the screen");
 app.semantic_intent("music.pause","touch");
 require(std::holds_alternative<wire::PauseRequest>(next_request()),"Pause");
 app.semantic_intent("music.next","touch");
 require(std::holds_alternative<wire::NextRequest>(next_request()),"Skip");
 app.semantic_intent("music.volume_up","touch");
 const auto louder=next_request();
 require(std::holds_alternative<wire::VolumeRequest>(louder)&&std::get<wire::VolumeRequest>(louder).percent==45,"Volume + asks for ten more");
 require(app.scene.music.volume_percent==45,"the new volume shows at once");
 send(wire::Refused{"volume","MUSIC.RATE_LIMITED"});
 until([&]{return app.scene.music.notice=="Spotify is busy. Try again in a minute.";},"a refusal is shown in words");
 // Spotify text the panel's fonts cannot draw is not drawn, and long text is cut.
 state.playback=wire::Playback{false,"\xE5\xA4\x9C\xE6\x98\x8E\xE3\x81\x91",std::string(80,'x')};
 send(state);
 until([&]{return app.scene.music.state=="paused";},"paused");
 require(app.scene.music.title=="Title not shown","a title outside the panel's fonts is replaced");
 require(app.scene.music.artists==std::string(60,'x')+"\xE2\x80\xA6","long text is cut with an ellipsis");
 app.render();
 require(has_target("music.play"),"paused shows Play");
 state.output.state="missing";state.playback.reset();
 send(state);
 until([&]{return app.scene.music.state=="missing";},"a sleeping speaker is shown as not available");
 require(has_target("music.use_speaker"),"Try again is on the screen");
 ::close(pair[1]);
 until([&]{return app.scene.music.state=="unavailable";},"a lost service is shown");
}
// The report screen on the reporter protocol, with this test as bhp-reporter on
// the other end of a socket pair (the reporter itself is tested in bug_reporter).
void test_report_files_through_the_reporter(const std::filesystem::path &root){
 namespace wire=bug_reporter::protocol;
 PanelSimulation app(false);
 app.load(bhp::simulator::read_scenario((root/"simulator/scenarios/music-playing.json").string()));
 const auto has_target=[&app](const std::string &intent){
  for(const auto &target:app.display.hit_targets())if(target.intent_id==intent)return true;
  return false;
 };
 require(has_target("shell.open_report"),"every day surface has the Report button");
 app.semantic_intent("shell.open_report","touch");
 require(app.scene.surface_id=="report"&&app.scene.report.state=="unavailable","without the reporter nothing is saved");
 app.semantic_intent("report.done","touch");
 require(app.scene.surface_id=="music","Done goes back where Report was tapped");
 int pair[2];
 require(::socketpair(AF_UNIX,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0,pair)==0,"socket pair");
 app.attach_bug_reporter_peer(pair[0],::getuid());
 bhp::media_window_protocol::FrameChannel reporter(pair[1],wire::kMaximumFrameBytes);
 const auto until=[&app](const std::function<bool()> &done,const char *message){
  for(int i=0;i<400&&!done();++i){app.service();std::this_thread::sleep_for(std::chrono::milliseconds(5));}
  require(done(),message);
 };
 const auto next_request=[&]()->wire::UiRequest{
  for(int i=0;i<400;++i){
   app.service();
   auto frame=reporter.receive();
   require(frame.has_value(),"the UI keeps the reporter connection");
   if(frame.value()){auto request=wire::decode_ui_request(*frame.value());require(request.has_value(),"a valid request");return request.value();}
   std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  throw std::runtime_error("no request reached the reporter");
 };
 const auto send=[&](const wire::ReporterEvent &event){require(reporter.send(wire::encode(event).value()).has_value(),"event sent");};
 require(std::holds_alternative<wire::StatusRequest>(next_request()),"the UI asks for the list when it attaches");
 app.semantic_intent("shell.open_report","touch");
 const auto snapshot=next_request();
 require(std::holds_alternative<wire::SnapshotRequest>(snapshot),"Report snapshots the moment of the tap");
 const auto &screen=std::get<wire::SnapshotRequest>(snapshot).screen;
 require(screen.surface=="music"&&screen.state=="playing"&&screen.display_mode=="day_active"&&screen.presence=="present","with the screen and lifecycle it was pressed on");
 require(app.scene.surface_id=="report"&&app.scene.report.state=="compose"&&app.scene.report.text.empty(),"the form opens empty");
 require(!has_target("shell.open_report"),"the report screen has no Report button of its own");
 app.semantic_intent("report.send","touch");
 require(app.scene.report.notice=="Still getting ready. Try Send again.","Send waits for the snapshot");
 send(wire::SnapshotTaken{"snap-0123456789abcdef"});
 const auto settle=[&app]{for(int i=0;i<20;++i){app.service();std::this_thread::sleep_for(std::chrono::milliseconds(5));}};
 settle();
 for(const auto *key:{"report.key.h","report.key.i","report.key.space","report.key.digit_2","report.key.question","report.key.x","report.key.backspace"})app.semantic_intent(key,"touch");
 require(app.scene.report.text=="hi 2?","keys type and Delete removes");
 app.semantic_intent("report.send","touch");
 const auto submit=next_request();
 require(std::holds_alternative<wire::SubmitRequest>(submit)&&std::get<wire::SubmitRequest>(submit).snapshot_id=="snap-0123456789abcdef"&&std::get<wire::SubmitRequest>(submit).text=="hi 2?","Send submits the text with the snapshot");
 require(app.scene.report.state=="sending","sending");
 wire::Reports reports;reports.items={{"rpt-0123456789abcdef0123456789abcdef","text","fix_in_pr",81u,"2026-11-08"}};reports.update_ready=true;
 send(wire::Submitted{"rpt-0123456789abcdef0123456789abcdef"});
 send(reports);
 until([&]{return app.scene.report.state=="sent";},"saved");
 require(app.scene.report.lines==std::vector<std::string>{"Typed report · Fix in PR #81 · 2026-11-08"},"the list in words");
 app.semantic_intent("report.done","touch");
 require(app.scene.surface_id=="music"&&app.scene.report.update_ready,"back on the music screen, with the update dot");
 app.semantic_intent("shell.open_report","touch");
 (void)next_request();
 send(wire::SnapshotTaken{"snap-1123456789abcdef"});
 settle();
 app.semantic_intent("report.send","touch");
 (void)next_request();
 send(wire::Refused{"submit","REPORT.RATE_LIMITED"});
 until([&]{return app.scene.report.notice=="One report a minute. Try again shortly.";},"a refusal in words");
 require(app.scene.report.state=="compose","and the form stays");
 app.semantic_intent("report.cancel","touch");
 require(std::holds_alternative<wire::CancelRequest>(next_request())&&app.scene.surface_id=="music","Cancel forgets the snapshot");
 ::close(pair[1]);
 app.semantic_intent("shell.open_report","touch");
 until([&]{return app.scene.report.state=="unavailable";},"a lost reporter is shown");
}
// Step 3: "Lexi report a bug" opens the report screen (voice never records), a touch
// records a description through the broker under bug_report, and Send names the take.
void test_report_by_voice_with_a_spoken_description(const std::filesystem::path &root){
 namespace wire=bug_reporter::protocol;
 PanelSimulation app(false);
 app.load(bhp::simulator::read_scenario((root/"simulator/scenarios/day-front.json").string()));
 int pair[2];
 require(::socketpair(AF_UNIX,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0,pair)==0,"socket pair");
 app.attach_bug_reporter_peer(pair[0],::getuid());
 bhp::media_window_protocol::FrameChannel reporter(pair[1],wire::kMaximumFrameBytes);
 const auto until=[&app](const std::function<bool()> &done,const char *message){
  for(int i=0;i<600&&!done();++i){app.service();std::this_thread::sleep_for(std::chrono::milliseconds(10));}
  require(done(),message);
 };
 const auto next_request=[&]()->wire::UiRequest{
  for(int i=0;i<400;++i){
   app.service();
   auto frame=reporter.receive();
   require(frame.has_value(),"the UI keeps the reporter connection");
   if(frame.value()){auto request=wire::decode_ui_request(*frame.value());require(request.has_value(),"a valid request");return request.value();}
   std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  throw std::runtime_error("no request reached the reporter");
 };
 require(std::holds_alternative<wire::StatusRequest>(next_request()),"the UI asks for the list");
 app.event({{"type","utterance"},{"phrase","Lexi report a bug"}});
 until([&]{return app.scene.surface_id=="report";},"the spoken command opens the report screen");
 const auto snapshot=next_request();
 require(std::holds_alternative<wire::SnapshotRequest>(snapshot)&&std::get<wire::SnapshotRequest>(snapshot).screen.surface=="study",
         "with the study screen it was said on");
 require(app.scene.report.state=="compose","voice opens the form; it never starts a recording");
 require(reporter.send(wire::encode(wire::ReporterEvent(wire::SnapshotTaken{"snap-2123456789abcdef"})).value()).has_value(),"snapshot");
 app.semantic_intent("report.record","touch");
 require(app.scene.report.state=="recording","a touch records a description");
 until([&]{return app.scene.report.elapsed_ms>0;},"levels arrive");
 app.semantic_intent("report.stop","touch");
 until([&]{return app.scene.report.has_recording;},"the description is kept");
 require(app.scene.report.state=="compose","back to the form, the recording attached");
 std::string handed;
 for(const auto &entry:std::filesystem::directory_iterator(app.voice_lab_report_handoff_root()))handed=entry.path().filename().string();
 require(handed.rfind("take-",0)==0,"the broker handed the take to the reporter's directory");
 require(std::filesystem::is_empty(app.voice_lab_handoff_root()),"never to the voice corpus's");
 app.semantic_intent("report.send","touch");
 const auto submit=next_request();
 require(std::holds_alternative<wire::SubmitRequest>(submit)&&std::get<wire::SubmitRequest>(submit).take_id==handed,
         "Send names the spoken description's take");

 // The gate closes while recording: nothing is kept, and the report can still go in words.
 require(reporter.send(wire::encode(wire::ReporterEvent(wire::Submitted{"rpt-1123456789abcdef0123456789abcdef"})).value()).has_value(),"submitted");
 until([&]{return app.scene.report.state=="sent";},"sent");
 app.semantic_intent("report.done","touch");
 app.semantic_intent("shell.open_report","touch");
 (void)next_request();
 require(reporter.send(wire::encode(wire::ReporterEvent(wire::SnapshotTaken{"snap-3123456789abcdef"})).value()).has_value(),"snapshot");
 app.semantic_intent("report.record","touch");
 until([&]{return app.scene.report.elapsed_ms>0;},"recording again");
 app.event({{"type","lifecycle"},{"state","grace"}});
 until([&]{return app.scene.report.notice=="Recording stopped. Nothing was kept.";},"a closed gate ends the recording");
 require(app.scene.report.state=="compose"&&!app.scene.report.has_recording,"with nothing attached");
 ::close(pair[1]);
}
void test_home_navigation_preserves_durable_requests(const std::filesystem::path &root){
 using namespace vocabulary_panel;
 const auto directory=std::filesystem::temp_directory_path()/("bhp-home-retention-"+new_study_uuid());
 const auto state_file=directory/"study-state.json";
 std::string request_bytes;
 {
  FlashCardSession session(std::make_shared<FileStudyStore>(directory.string()));
  const auto fixture=decode_study_session(read_bytes(root/"vocabulary_panel/tests/fixtures/study_v1/examples/session-created.json"));
  require(fixture.has_value()&&session.install_session(fixture.value()).has_value(),"navigation fixture installs durably");
  for(const auto intent:{StudyIntent::flip,StudyIntent::rate_good})
   require(session.apply_intent(intent,StudyInputSource::touch,session.current_binding()).has_value(),"navigation fixture has a pending rating");
  const auto request=session.prepare_sync();require(request.has_value(),"navigation fixture persists a sync request");
  const auto encoded=encode_study_sync(request.value());require(encoded.has_value(),"navigation fixture request encodes");request_bytes=encoded.value();
 }
 {
  PanelSimulation app(false,directory.string());
  app.load(bhp::simulator::read_scenario((root/"simulator/scenarios/day-front.json").string()));
  const auto before=bhp::simulator::Json::parse(read_bytes(state_file));
  const auto frame=app.scene.study_state;
  const auto generation=app.render_generation;
  app.semantic_intent("shell.home","touch");
  require(app.scene.surface_id=="home"&&app.render_generation>generation,"Home receives a fresh shell generation");
  require(!app.scene.can_undo&&app.status().at("pending_review_events")==1,"leaving study closes Undo without discarding the rating");
  app.semantic_intent("shell.open_agenda","host_fixture");
  require(app.scene.surface_id=="agenda","Home opens the Agenda preview through its surface contract");
  app.semantic_intent("study.flip","host_fixture");
  require(app.scene.study_state==frame,"Agenda cannot route study feature actions");
  app.semantic_intent("shell.home","host_fixture");
  app.semantic_intent("shell.open_study","touch");
  require(app.scene.surface_id=="study"&&app.scene.study_state==frame,"Home returns to the retained study position");
  const auto after=bhp::simulator::Json::parse(read_bytes(state_file));
  for(const auto *field:{"stream","session","history","cursor","next_item","items","pending","in_flight","initial_request","successor_request","staged_successor","rejected_receipts"})
   require(after.at(field)==before.at(field),"app navigation preserves durable study history, event bytes and request identities");
 }
 {
  FlashCardSession restored(std::make_shared<FileStudyStore>(directory.string()));
  require(restored.restore().has_value(),"navigated durable study restores");
  const auto request=restored.prepare_sync();require(request.has_value(),"navigated request resumes");
  const auto encoded=encode_study_sync(request.value());require(encoded.has_value()&&encoded.value()==request_bytes,"app switching and restart retain the exact original sync request");
 }
 std::filesystem::remove_all(directory);
}
void test_home_navigation_gates_and_retains_voice_state(const std::filesystem::path &root){
 PanelSimulation app(false);
 const auto home=bhp::simulator::read_scenario((root/"simulator/scenarios/home.json").string());
 app.load(home);
 require(app.scene.surface_id=="home"&&!app.scene.weather_available,"default Home has no invented weather");
 app.semantic_intent("shell.open_study","voice");
 require(app.scene.surface_id=="home","direct voice navigation is rejected");
 for(const auto *state:{"night","blank","clock_untrusted"}){
  app.event({{"type","lifecycle"},{"state",state}});
  for(const auto *source:{"touch","host_fixture"})for(const auto *intent:{"shell.home","shell.open_study","shell.open_agenda"}){
   app.semantic_intent(intent,source);
   require(app.scene.surface_id=="home"&&app.scene.acknowledgement=="Action unavailable","non-day lifecycle rejects all shell navigation sources");
  }
 }
 app.accept_lifecycle(app.supervisor.publish("day"));app.render();
 app.invalidate_lifecycle();app.semantic_intent("shell.open_study","touch");
 require(app.scene.surface_id=="home","lifecycle loss blocks app launch");
 app.accept_lifecycle(app.supervisor.publish("day"));app.render();
 app.scene.fixture_clock_untrusted=true;app.semantic_intent("shell.open_agenda","host_fixture");
 require(app.scene.surface_id=="home","fixture diagnostic blocks app launch");
 app.scene.fixture_clock_untrusted=false;
 app.semantic_intent("shell.open_study","touch");
 require(app.scene.surface_id=="study","fresh lifecycle permits app launch");
 app.semantic_intent("study.flip","touch");app.semantic_intent("study.next","touch");
 app.semantic_intent("shell.home","touch");app.semantic_intent("shell.open_study","touch");
 app.semantic_intent("study.back","touch");
 require(app.scene.study_state=="back"&&app.scene.card_word=="serendipity","study history and remembered card face survive app switching");
 app.load(bhp::simulator::read_scenario((root/"simulator/scenarios/day-front.json").string()));
 auto generation=app.render_generation;
 app.event({{"type","utterance"},{"phrase","Lexi flip"}});
 for(int i=0;i<100&&app.render_generation==generation;++i){app.service();std::this_thread::sleep_for(std::chrono::milliseconds(10));}
 require(app.scene.provisional,"fresh unconfirmed voice reveal is provisional before departure");
 app.semantic_intent("shell.home","touch");
 require(!app.scene.provisional&&!app.scene.can_undo,"Home releases provisional evidence and Undo");
 app.semantic_intent("shell.open_study","touch");
 require(app.scene.study_state=="resume"&&!app.scene.provisional,"returning to voice-only evidence requires explicit resume");
 app.semantic_intent("study.flip","touch");
 require(app.scene.study_state=="back"&&!app.scene.provisional,"touch resume confirms the retained card");
 app.semantic_intent("study.flip","touch");
 std::this_thread::sleep_for(std::chrono::milliseconds(310));
 app.event({{"type","utterance"},{"phrase","Lexi flip"}});
 app.semantic_intent("shell.home","touch");app.semantic_intent("shell.open_study","touch");
 for(int i=0;i<35;++i){app.service();std::this_thread::sleep_for(std::chrono::milliseconds(10));}
 require(app.scene.study_state=="front"&&!app.scene.provisional,"old voice result cannot act after leaving and returning to study");
 app.load(bhp::simulator::read_scenario((root/"simulator/scenarios/study-rejected.json").string()));
 app.semantic_intent("shell.home","touch");app.semantic_intent("shell.open_study","touch");
 require(app.scene.delivery_status=="Not saved","app switching preserves terminal review rejection");
}
void test_home_weather_scenario_validation(const std::filesystem::path &root){
 auto scenario=bhp::simulator::read_scenario((root/"simulator/scenarios/home-weather.json").string());
 PanelSimulation app(false);app.load(scenario);
 require(app.scene.weather_available&&!app.scene.weather_stale&&app.scene.weather_fixture&&app.scene.weather_high_celsius==24&&app.scene.weather_low_celsius==12&&app.scene.weather_temperature_unit=="F","Home projects only the explicitly supplied weather fixture");
 const auto path=std::filesystem::temp_directory_path()/("bhp-home-weather-"+vocabulary_panel::new_study_uuid()+".json");
 auto parse=[&](const auto &value){std::ofstream file(path);file<<value.dump();file.close();return bhp::simulator::read_scenario(path.string());};
 for(const auto &weather:std::vector<bhp::simulator::Json>{
  {{"state","available"},{"high_celsius",12}},
  {{"state","stale"},{"high_celsius",12},{"low_celsius",24}},
  {{"state","available"},{"high_celsius",61},{"low_celsius",12}},
  {{"state","available"},{"high_celsius",24},{"low_celsius",-81}},
  {{"state","available"},{"high_celsius",24.5},{"low_celsius",12}},
  {{"state","available"},{"high_celsius",24},{"low_celsius",12},{"unit","R"}},
  {{"state","available"},{"high_celsius",24},{"low_celsius",12},{"bar","sideways"}},
  {{"state","available"},{"high_celsius",24},{"low_celsius",12},{"sunrise","7:12"}},
  {{"state","unavailable"},{"provider","invented"}}}){
  auto invalid=scenario;invalid["weather"]=weather;bool rejected=false;try{parse(invalid);}catch(const std::exception &){rejected=true;}require(rejected,"closed weather schema rejects invalid or fabricated observations");
 }
 scenario["weather"]["state"]="stale";scenario["weather"]["unit"]="C";app.load(parse(scenario));
 require(app.scene.weather_available&&app.scene.weather_stale&&app.scene.weather_temperature_unit=="C","stale weather is explicitly marked and retains its unit");
 scenario["weather"]={{"state","unavailable"}};app.load(parse(scenario));
 require(!app.scene.weather_available&&!app.scene.weather_stale,"unavailable weather clears previous observations");
 scenario["fixture_clock"]="24:00";bool rejected=false;try{parse(scenario);}catch(const std::exception &){rejected=true;}require(rejected,"fixture clock rejects out-of-range civil hour");
 std::filesystem::remove(path);
}
void test_durable_delivery_fixture_isolation(const std::filesystem::path &root){
 using namespace vocabulary_panel;
 const auto directory=std::filesystem::temp_directory_path()/("bhp-delivery-isolation-"+new_study_uuid());
 const auto state_file=directory/"study-state.json";
 std::string saved_bytes,request_bytes;
 {
  auto store=std::make_shared<FileStudyStore>(directory.string());FlashCardSession session(store);
  const auto fixture=decode_study_session(read_bytes(root/"vocabulary_panel/tests/fixtures/study_v1/examples/session-created.json"));
  require(fixture.has_value()&&session.install_session(fixture.value()).has_value(),"canonical fixture session installs durably");
  for(const auto intent:{StudyIntent::flip,StudyIntent::rate_good})
   require(session.apply_intent(intent,StudyInputSource::touch,session.current_binding()).has_value(),"durable rating fixture transition succeeds");
  const auto request=session.prepare_sync();require(request.has_value(),"pending rating has a persisted sync request");
  const auto encoded=encode_study_sync(request.value());require(encoded.has_value(),"pending sync is canonical wire data");
  request_bytes=encoded.value();saved_bytes=read_bytes(state_file);
 }
 {
  PanelSimulation protected_app(false,directory.string());
  for(const auto *state:{"queued","saved","rejected"}){
   const auto scenario=bhp::simulator::read_scenario((root/(std::string("simulator/scenarios/study-")+state+".json")).string());
   bool rejected=false;
   try{protected_app.load(scenario);}catch(const std::runtime_error &error){rejected=std::string(error.what())=="SIMULATOR.DELIVERY_FIXTURE_REQUIRES_MEMORY";}
   require(rejected,"durable startup rejects synthetic delivery fixtures before restore");
   require(read_bytes(state_file)==saved_bytes,"fixture rejection preserves pending rating and request bytes exactly");
   protected_app.load(scenario,true,true);
   require(protected_app.status().at("fixture_override")==true,"explicit fixture selection uses isolated memory state");
   require(read_bytes(state_file)==saved_bytes,"isolated delivery fixture cannot change the durable outbox");
  }
 }
 {
  FlashCardSession restored(std::make_shared<FileStudyStore>(directory.string()));
  require(restored.restore().has_value(),"original pending rating survives fixture attempts");
  require(std::count_if(restored.pending_events().begin(),restored.pending_events().end(),[](const auto &event){return event.event_kind=="rated";})==1,"pending rating remains unacknowledged");
  const auto request=restored.prepare_sync();require(request.has_value(),"original sync resumes");
  const auto encoded=encode_study_sync(request.value());
  require(encoded.has_value()&&encoded.value()==request_bytes,"restart replays the exact original canonical sync request");
 }
 std::filesystem::remove_all(directory);
}
// Review Q7: the schedule a lost service is tried again on.
void test_reconnect_schedule(){
 auto now=std::chrono::steady_clock::time_point{};
 bhp::simulator::ReconnectSchedule schedule([&]{return now;});
 require(schedule.due(),"a first try is allowed at once");
 int waited=0;
 for(const int seconds:{1,2,4,8,16,32,60,60}){
  schedule.failed();
  now+=std::chrono::seconds(seconds)-std::chrono::milliseconds(1);
  require(!schedule.due(),"not before the delay");
  now+=std::chrono::milliseconds(1);
  require(schedule.due(),"due once the delay has passed");
  waited+=seconds;
 }
 require(waited==183&&schedule.next_delay()==std::chrono::seconds(60),"1 s doubling, capped at 60 s");
 schedule.succeeded();
 schedule.failed();
 now+=std::chrono::seconds(1);
 require(schedule.due()&&schedule.next_delay()==std::chrono::seconds(2),"a working connection resets it to 1 s");
}
// A listening service socket at a path, which the test opens and closes.
struct ServiceSocket{
 std::string path;int listener{-1};
 explicit ServiceSocket(std::string socket_path):path(std::move(socket_path)){}
 void open(){
  listener=::socket(AF_UNIX,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);sockaddr_un address{};address.sun_family=AF_UNIX;
  std::memcpy(address.sun_path,path.c_str(),path.size()+1);
  require(listener>=0&&::bind(listener,reinterpret_cast<sockaddr*>(&address),sizeof address)==0&&::listen(listener,4)==0,"service listens");
 }
 int accept_one(){
  for(int i=0;i<200;++i){const int peer=::accept4(listener,nullptr,nullptr,SOCK_NONBLOCK|SOCK_CLOEXEC);if(peer>=0)return peer;std::this_thread::sleep_for(std::chrono::milliseconds(5));}
  throw std::runtime_error("the UI did not connect");
 }
 void close_it(){if(listener>=0){::close(listener);listener=-1;}::unlink(path.c_str());}
 ~ServiceSocket(){close_it();}
};
// Review Q7: music and the reporter may be down at start, and may go and come back.
void test_services_come_and_go(const std::filesystem::path &root){
 namespace music=music_control::protocol;
 const auto directory=std::filesystem::temp_directory_path()/("bhp-reconnect-"+std::to_string(::getpid()));
 std::filesystem::create_directories(directory);
 ServiceSocket music_socket((directory/"music.sock").string()),reporter_socket((directory/"reporter.sock").string());
 PanelSimulation app(false);
 auto now=std::chrono::steady_clock::time_point{}+std::chrono::hours(1);
 app.set_reconnect_clock([&]{return now;});
 app.load(bhp::simulator::read_scenario((root/"simulator/scenarios/home.json").string()));
 app.attach_music_control(music_socket.path,::getuid());
 app.attach_bug_reporter(reporter_socket.path,::getuid());
 require(app.scene.music.state=="unavailable","a music service that is down at start is shown unavailable, not fatal");
 app.semantic_intent("shell.open_report","touch");
 require(app.scene.report.state=="unavailable","a reporter that is down at start: Report says unavailable");
 app.load(bhp::simulator::read_scenario((root/"simulator/scenarios/home.json").string()));
 require(app.scene.music.state=="unavailable","a scenario load keeps the live service's absence");

 music_socket.open();reporter_socket.open();
 app.service();
 require(app.scene.music.state=="unavailable","nothing is tried before the first second has passed");
 now+=std::chrono::seconds(1);
 app.service();
 require(app.scene.music.state=="checking","the service is up: connected at the next try");
 int music_peer=music_socket.accept_one();
 const int reporter_peer=reporter_socket.accept_one();
 bhp::media_window_protocol::FrameChannel service(music_peer,music::kMaximumFrameBytes);
 music::State state;state.sequence=1;state.sign_in="signed_in";state.output={"available","Living Room Bose",true,35u};
 require(service.send(music::encode(music::MusicEvent{state}).value()).has_value(),"state sent");
 for(int i=0;i<200&&app.scene.music.state!="ready";++i){app.service();std::this_thread::sleep_for(std::chrono::milliseconds(5));}
 require(app.scene.music.state=="ready","its state is shown");

 ::close(music_peer);music_socket.close_it();
 for(int i=0;i<200&&app.scene.music.state!="unavailable";++i){app.service();std::this_thread::sleep_for(std::chrono::milliseconds(5));}
 require(app.scene.music.state=="unavailable","a lost service is shown unavailable");
 now+=std::chrono::seconds(1);app.service();
 require(app.scene.music.state=="unavailable","after a working connection the next try is 1 s later, and fails");
 now+=std::chrono::seconds(1);app.service();
 require(app.scene.music.state=="unavailable","then 2 s: not yet");
 music_socket.open();
 now+=std::chrono::seconds(1);app.service();
 require(app.scene.music.state=="checking","the service is back: reconnected without a restart");
 music_peer=music_socket.accept_one();::close(music_peer);
 // The reporter never sent an event, so its schedule kept growing: 1 s, then 2 s.
 ::close(reporter_peer);
 for(int i=0;i<20;++i){app.service();std::this_thread::sleep_for(std::chrono::milliseconds(5));}
 now+=std::chrono::seconds(1);app.service();
 require(::accept4(reporter_socket.listener,nullptr,nullptr,SOCK_CLOEXEC)<0,"the reporter is not tried before its delay");
 now+=std::chrono::seconds(1);app.service();
 ::close(reporter_socket.accept_one());
 std::filesystem::remove_all(directory);
}
void test_weather_menu_and_the_worker(const std::filesystem::path &root){
 // Fixture weather: the forecast on the bar opens the menu from any day surface.
 PanelSimulation app(false);
 app.load(bhp::simulator::read_scenario((root/"simulator/scenarios/music-playing.json").string()));
 const auto has_target=[&app](const std::string &intent){
  for(const auto &target:app.display.hit_targets())if(target.intent_id==intent)return true;
  return false;
 };
 require(has_target("shell.open_weather"),"the forecast on the day bar opens the weather menu");
 app.tap(848,36);
 require(app.scene.surface_id=="weather"&&app.scene.weather.state=="menu","a tap on the forecast opens the menu");
 require(!has_target("shell.open_weather"),"on the menu itself the forecast does nothing");
 app.semantic_intent("weather.unit.kelvin","touch");
 require(app.scene.weather_temperature_unit=="K","K changes the bar's unit");
 app.semantic_intent("weather.bar.sun_times","touch");
 require(app.scene.weather_bar=="sun_times","Sunrise & sunset swaps the bar");
 app.semantic_intent("weather.done","touch");
 require(app.scene.surface_id=="music","Done returns where the forecast was tapped");

 // Live: a worker on the recorded answers, its store, and fixed clocks.
 const auto directory=std::filesystem::temp_directory_path()/("bhp-weather-ui-"+vocabulary_panel::new_study_uuid());
 std::filesystem::create_directories(directory); // DurableFile makes only the last level, 0700
 auto monotonic=std::make_shared<std::atomic<std::int64_t>>(1000);
 const std::int64_t utc=1794337200; // 2026-11-10T19:00:00Z, the recorded forecast's day in Denver
 auto worker=std::make_shared<weather_panel::WeatherWorker>(
  std::make_shared<weather_panel::FixtureWeatherClient>((root/"weather_panel/tests/fixtures").string()),
  std::make_shared<weather_panel::WeatherStore>((directory/"state").string()),
  weather_panel::WeatherClocks{[monotonic]{return monotonic->load();},[utc]{return utc;}},nullptr,std::chrono::milliseconds(5));
 app.set_weather_clock([utc]{return utc;});
 app.attach_weather(worker);
 const auto until=[&app](const std::function<bool()> &done,const char *message){
  for(int i=0;i<400&&!done();++i){app.service();std::this_thread::sleep_for(std::chrono::milliseconds(5));}
  require(done(),message);
 };
 until([&]{return app.scene.weather_available;},"the live forecast reaches the bar");
 require(!app.scene.weather_fixture&&app.scene.weather_high_celsius==20.4&&app.scene.weather_low_celsius==3.5,
  "the bar shows the parsed forecast, not a fixture");
 require(app.scene.weather_temperature_unit=="F"&&app.scene.weather_bar=="temperatures","the defaults are F and high/low");
 app.semantic_intent("shell.open_weather","touch");
 require(app.scene.weather.automatic&&app.scene.weather.location_label=="Boulder, Colorado","Automatic names the area");
 app.semantic_intent("weather.unit.celsius","touch");
 until([&]{return worker->snapshot().settings.unit==weather_panel::TemperatureUnit::celsius;},"a unit tap changes the settings");
 app.semantic_intent("weather.location.choose","touch");
 require(app.scene.weather.state=="search"&&app.scene.weather.query.empty(),"Choose a place opens the search");
 for(const auto *key:{"weather.key.z","weather.key.z","weather.key.z"})app.semantic_intent(key,"touch");
 app.semantic_intent("weather.search.submit","touch");
 until([&]{return app.scene.weather.state=="no_results";},"an unknown name finds nothing");
 app.semantic_intent("weather.location.choose","touch");
 for(const auto *key:{"weather.key.b","weather.key.o","weather.key.u","weather.key.l"})app.semantic_intent(key,"touch");
 require(app.scene.weather.query=="boul","the drawn keyboard types");
 app.semantic_intent("weather.search.submit","touch");
 require(app.scene.weather.state=="searching","a search shows that it is searching");
 until([&]{return app.scene.weather.state=="results";},"the search answers");
 require(app.scene.weather.results.size()==5&&app.scene.weather.results[0]=="Boulder, Colorado, United States","up to five places");
 app.semantic_intent("weather.result.2","touch");
 require(app.scene.weather.state=="menu"&&!app.scene.weather.automatic&&app.scene.weather.location_label=="Boulder, Montana, United States",
  "picking a place shows it in the menu");
 until([&]{const auto s=worker->snapshot();return s.settings.place&&s.settings.place->region=="Montana";},"picking a place stores it");
 until([&]{return app.scene.weather_available;},"and a refresh brings its forecast");
 // The worker's store holds the files' locks until it is gone, as a restart would release them.
 app.attach_weather(nullptr);
 worker.reset();
 const auto saved=weather_panel::WeatherStore((directory/"state").string()).settings();
 require(saved.has_value()&&saved.value().place&&saved.value().place->region=="Montana"&&saved.value().unit==weather_panel::TemperatureUnit::celsius,
  "the settings survive a restart");
 std::filesystem::remove_all(directory);
}
void test_weather_sources(const std::filesystem::path &root){
 PanelSimulation app(false);
 app.load(bhp::simulator::read_scenario((root/"simulator/scenarios/home.json").string()));
 const auto refused=[&app](const std::string &source,const std::string &directory,const std::string &code){
  try{app.attach_weather_source(source,directory);}catch(const std::exception &error){return std::string(error.what())==code;}
  return false;
 };
 require(refused("weather-api","","SIMULATOR.WEATHER_SOURCE_INVALID"),"only fixture and live are sources");
 require(refused("live","","SIMULATOR.WEATHER_STATE_DIRECTORY_REQUIRED"),"live weather keeps its settings in a directory");
 app.attach_weather_source("fixture","");
 app.semantic_intent("shell.open_weather","touch");
 app.semantic_intent("weather.location.choose","touch");
 for(const auto *key:{"weather.key.b","weather.key.o","weather.key.u","weather.key.l"})app.semantic_intent(key,"touch");
 app.semantic_intent("weather.search.submit","touch");
 for(int i=0;i<400&&app.scene.weather.state!="results";++i){app.service();std::this_thread::sleep_for(std::chrono::milliseconds(5));}
 require(app.scene.weather.state=="results"&&app.scene.weather.results.size()==5,"the fixture source answers from the recorded files");
 app.attach_weather(nullptr);
}
int main(){try{
 const auto root=std::filesystem::path(BHP_SIMULATOR_SOURCE_ROOT);
 test_durable_delivery_fixture_isolation(root);
 test_home_navigation_preserves_durable_requests(root);
 test_home_navigation_gates_and_retains_voice_state(root);
 test_home_weather_scenario_validation(root);
 test_voice_lab_records_through_the_broker(root);
 test_voice_lab_follows_the_uploader(root);
 test_music_follows_the_service(root);
 test_report_files_through_the_reporter(root);
 test_report_by_voice_with_a_spoken_description(root);
 test_reconnect_schedule();
 test_services_come_and_go(root);
 test_weather_menu_and_the_worker(root);
 test_weather_sources(root);
 PanelSimulation app(false);
 const auto front=bhp::simulator::read_scenario((root/"simulator/scenarios/day-front.json").string());
 app.load(front);
 require(app.scene.study_state=="front","fixture first presentation is front");
 require(app.scene.delivery_status.empty()&&app.status().at("pending_review_events")==0,"routine first presentation has no delivery badge");
 require(app.scene.fixture_data,"synthetic study is explicitly fixture data");
 app.semantic_intent("study.rate.good","touch");
 require(app.scene.study_state=="front"&&!app.scene.can_undo&&app.scene.acknowledgement=="STUDY.RATING_UNAVAILABLE","front-side rating is visibly rejected without advancing");
 app.semantic_intent("study.back","touch");
 require(app.scene.study_state=="front"&&!app.scene.can_back&&app.scene.acknowledgement=="At the first card","first-card Back is a visible boundary no-op");
 app.tap(512,300);require(app.scene.study_state=="back","shell touch hit map flips through reducer");
 require(app.scene.delivery_status.empty()&&!app.scene.provisional,"touch reveal bookkeeping stays quiet");
 app.semantic_intent("study.next","touch");require(app.scene.study_state=="front","next starts front");
 app.semantic_intent("study.flip","touch");
 app.semantic_intent("study.back","touch");require(app.scene.study_state=="back","history restores exact remembered face");
 app.semantic_intent("study.next","touch");require(app.scene.study_state=="back","forward history reuses the previously revealed card face");
 app.semantic_intent("study.back","touch");
 app.semantic_intent("study.rate.good","touch");require(app.scene.can_undo,"rating creates live touch undo");
 require(app.scene.delivery_status=="Waiting to sync"&&app.status().at("pending_review_events")==1,"only an actual pending rating shows waiting");
 app.semantic_intent("study.rating.undo","host_fixture");require(app.scene.can_undo,"host command cannot consume touch-only Undo");
 app.semantic_intent("study.rating.undo","touch");require(!app.scene.can_undo,"touch Undo consumes one lease");
 app.semantic_intent("study.rating.undo","touch");require(!app.scene.can_undo&&app.scene.acknowledgement=="STUDY.UNDO_UNAVAILABLE","consumed Undo cannot mutate history twice");
 app.load(bhp::simulator::read_scenario((root/"simulator/scenarios/study-queued.json").string()));
 require(app.scene.delivery_status=="Waiting to sync"&&!app.scene.provisional&&app.status().at("pending_review_events")==1,"queued scenario contains a canonical pending rating, not provisional evidence");
 app.load(bhp::simulator::read_scenario((root/"simulator/scenarios/study-saved.json").string()));
 require(app.scene.delivery_status.empty()&&app.scene.can_undo&&app.status().at("pending_review_events")==0,"acknowledged rating scenario is quiet and retains its real Undo lease");
 app.semantic_intent("study.rating.undo","touch");
 require(app.scene.delivery_status=="Waiting to sync"&&app.status().at("pending_review_events")==1,"an actual pending retraction also shows waiting");
 app.load(bhp::simulator::read_scenario((root/"simulator/scenarios/study-rejected.json").string()));
 require(app.scene.delivery_status=="Not saved"&&!app.scene.can_undo&&app.status().at("pending_review_events")==0,"terminal receipt removes pending work but retains failure notice");
 app.service();app.render();
 require(app.scene.delivery_status=="Not saved","redraw and idle service do not erase rejection");
 app.load(bhp::simulator::read_scenario((root/"simulator/scenarios/study-offline.json").string()));
 for(int i=0;i<7;++i)app.semantic_intent("study.next","touch");
 require(app.scene.study_state=="front"&&app.scene.can_back,"bounded cached cards remain available offline");
 app.semantic_intent("study.flip","touch");
 app.semantic_intent("study.next","touch");
 require(app.scene.study_state=="refill","offline cached page ends at the explicit refill sentinel");
 app.semantic_intent("study.next","touch");
 require(app.scene.study_state=="refill"&&app.scene.acknowledgement=="Action accepted","repeated Next is a boundary no-op without an error or invented card");
 app.semantic_intent("study.back","touch");
 require(app.scene.study_state=="back","Back from refill restores the exact remembered last-card face");
 app.load(front);
 app.invalidate_lifecycle();app.semantic_intent("study.flip","touch");app.service();
 require(app.scene.study_state=="front","disconnected lifecycle rejects feature actions");
 for(auto pixel:app.display.pixels())require((pixel&0xffffff)==0,"disconnected lifecycle stays black across service and render");
 app.accept_lifecycle(app.supervisor.publish("day"));app.render();
 auto before_voice=app.render_generation;
 app.event({{"type","utterance"},{"phrase","Lexi flip"}});
 for(int i=0;i<100&&app.render_generation==before_voice;++i){app.service();std::this_thread::sleep_for(std::chrono::milliseconds(10));}
 require(app.scene.study_state=="back"&&app.scene.provisional,"fake recognizer routes voice reveal as provisional");
 require(app.scene.delivery_status.empty()&&app.status().at("pending_review_events")==0,"provisional voice reveal is distinct from a queued rating");
 app.semantic_intent("study.rate.good","touch");require(app.scene.can_undo&&!app.scene.provisional,"touch rating promotes provisional evidence");
 app.semantic_intent("study.rating.undo","voice");require(app.scene.can_undo,"direct voice source cannot bypass broker");
 app.event({{"type","lifecycle"},{"state","night"}});auto frame=app.scene.study_state;
 app.semantic_intent("study.flip","touch");require(app.scene.study_state==frame,"night rejects feature input");
 app.scene.card_word="\xFF";app.render();
 require((app.display.pixels().front()&0xffffff)==app.scene.night_red_rgb888,"night background uses the configured red fixture field");
 bool black_clock_visible=false;
 for(auto pixel:app.display.pixels()){
  require((pixel&0xffff)==0,"night pixels contain no green or blue");
  if((pixel&0xffffff)==0)black_clock_visible=true;
 }
 require(black_clock_visible&&app.display.hit_targets().empty(),"night shows black clock glyphs on red without feature inputs");
 app.scene.fixture_clock_untrusted=true;app.render();
 for(auto pixel:app.display.pixels())require((pixel&0xffffff)==0,"untrusted night time leaves a black display");
 app.scene.fixture_clock_untrusted=false;
 app.event({{"type","lifecycle"},{"state","blank"}});for(auto pixel:app.display.pixels())require((pixel&0xffffff)==0,"blank surface is all black");
 app.load(bhp::simulator::read_scenario((root/"simulator/scenarios/presentation-failure.json").string()));
 app.semantic_intent("study.flip","touch");
 require(app.scene.study_state=="front"&&app.scene.acknowledgement=="Action unavailable","failed presentation rejects keyboard and semantic touch");
 app.semantic_intent("study.flip","host_fixture");
 require(app.scene.study_state=="front","failed presentation rejects host commands");
 const auto failed_generation=app.render_generation;
 app.event({{"type","utterance"},{"phrase","Lexi flip"}});
 for(int i=0;i<100&&app.render_generation==failed_generation;++i){app.service();std::this_thread::sleep_for(std::chrono::milliseconds(10));}
 require(app.scene.study_state=="front"&&!app.scene.provisional,"failed presentation keeps voice gate closed");
 for(auto pixel:app.display.pixels())require((pixel&0xffffff)==0,"failed presentation stays black after every input path");
 std::cout<<"{\"data\":{\"shell_reducer_surface_integration\":\"passed\"},\"error\":null,\"meta\":{\"simulation_not_hardware_evidence\":true}}\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
