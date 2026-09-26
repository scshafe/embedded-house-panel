#include "simulator/panel_simulation.hpp"
#include <iostream>
#include <chrono>
#include <stdexcept>
#include <thread>
#include <filesystem>
using bhp::simulator::PanelSimulation;
void require(bool condition,const char *message){if(!condition)throw std::runtime_error(message);}
int main(){try{
 PanelSimulation app(false);
 const auto root=std::filesystem::path(BHP_SIMULATOR_SOURCE_ROOT);
 const auto front=bhp::simulator::read_scenario((root/"simulator/scenarios/day-front.json").string());
 app.load(front);
 require(app.scene.study_state=="front","fixture first presentation is front");
 app.semantic_intent("study.rate.good","touch");
 require(app.scene.study_state=="front"&&!app.scene.can_undo&&app.scene.acknowledgement=="STUDY.RATING_UNAVAILABLE","front-side rating is visibly rejected without advancing");
 app.semantic_intent("study.back","touch");
 require(app.scene.study_state=="front"&&!app.scene.can_back&&app.scene.acknowledgement=="At the first card","first-card Back is a visible boundary no-op");
 app.tap(512,300);require(app.scene.study_state=="back","shell touch hit map flips through reducer");
 app.semantic_intent("study.next","touch");require(app.scene.study_state=="front","next starts front");
 app.semantic_intent("study.flip","touch");
 app.semantic_intent("study.back","touch");require(app.scene.study_state=="back","history restores exact remembered face");
 app.semantic_intent("study.next","touch");require(app.scene.study_state=="back","forward history reuses the previously revealed card face");
 app.semantic_intent("study.back","touch");
 app.semantic_intent("study.rate.good","touch");require(app.scene.can_undo,"rating creates live touch undo");
 app.semantic_intent("study.rating.undo","host_fixture");require(app.scene.can_undo,"host command cannot consume touch-only Undo");
 app.semantic_intent("study.rating.undo","touch");require(!app.scene.can_undo,"touch Undo consumes one lease");
 app.semantic_intent("study.rating.undo","touch");require(!app.scene.can_undo&&app.scene.acknowledgement=="STUDY.UNDO_UNAVAILABLE","consumed Undo cannot mutate history twice");
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
 app.semantic_intent("study.rate.good","touch");require(app.scene.can_undo&&!app.scene.provisional,"touch rating promotes provisional evidence");
 app.semantic_intent("study.rating.undo","voice");require(app.scene.can_undo,"direct voice source cannot bypass broker");
 app.event({{"type","lifecycle"},{"state","night"}});auto frame=app.scene.study_state;
 app.semantic_intent("study.flip","touch");require(app.scene.study_state==frame,"night rejects feature input");
 app.scene.card_word="\xFF";app.render();
 require((app.display.pixels().front()&0xffffff)==0x180000,"night RGB fixture is exact solid color");
 for(auto pixel:app.display.pixels())require((pixel&0xffffff)==0x180000,"night surface contains no feature pixels");
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
