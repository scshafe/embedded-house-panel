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
using bhp::simulator::PanelSimulation;
void require(bool condition,const char *message){if(!condition)throw std::runtime_error(message);}
std::string read_bytes(const std::filesystem::path &path){
 std::ifstream input(path,std::ios::binary);require(input.good(),"test state file must exist");
 return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
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
 require((app.display.pixels().front()&0xffffff)==0,"night background is black");
 bool red_clock_visible=false;
 for(auto pixel:app.display.pixels()){
  require((pixel&0xffff)==0,"night pixels contain no green or blue");
  if((pixel&0xff0000)!=0)red_clock_visible=true;
 }
 require(red_clock_visible&&app.display.hit_targets().empty(),"night shows only a red clock without feature inputs");
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
