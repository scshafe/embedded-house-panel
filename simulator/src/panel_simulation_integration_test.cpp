#include "simulator/panel_simulation.hpp"
#include "vocabulary_panel/study_wire_codec.h"
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
  {{"state","available"},{"high_celsius",24},{"low_celsius",12},{"unit","K"}},
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
int main(){try{
 const auto root=std::filesystem::path(BHP_SIMULATOR_SOURCE_ROOT);
 test_durable_delivery_fixture_isolation(root);
 test_home_navigation_preserves_durable_requests(root);
 test_home_navigation_gates_and_retains_voice_state(root);
 test_home_weather_scenario_validation(root);
 test_voice_lab_records_through_the_broker(root);
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
