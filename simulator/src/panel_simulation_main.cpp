#include "simulator/panel_simulation.hpp"
#include <chrono>
#include <deque>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <poll.h>
#include <unistd.h>
#include <thread>
#if BHP_NATIVE_WINDOW
#include <SDL.h>
#endif
namespace {
#if BHP_NATIVE_WINDOW
struct NativeInput {bhp::simulator::PanelSimulation *app;std::deque<std::string> keys;};
int observe_sdl(void *context,SDL_Event *event){auto *input=static_cast<NativeInput*>(context);
 if(event->type==SDL_MOUSEBUTTONDOWN||event->type==SDL_MOUSEBUTTONUP)input->app->supervisor.observe_pointer(event->type==SDL_MOUSEBUTTONDOWN,event->button.x,event->button.y);
 if(event->type==SDL_KEYDOWN&&!event->key.repeat)input->keys.push_back(SDL_GetKeyName(event->key.keysym.sym));
 if(event->type==SDL_QUIT)input->app->quit=true;return 1;}
#endif
void emit(const bhp::simulator::Json &data){std::cout<<bhp::simulator::Json{{"data",data},{"error",nullptr},{"meta",{{"service","panel_simulator"},{"simulation_not_hardware_evidence",true}}}}.dump()<<std::endl;}
}
int main(int argc,char **argv){try{
 std::string scenario,output,state_directory,lexinet_configuration,application_catalog;long exit_after=0;bool clock_preview=false;
 for(int i=1;i<argc;++i){std::string option=argv[i];if(option=="--help"){std::cout<<"--scenario FILE --output PNG (renderer) --screenshot PNG (simulator) --exit-after-ms N --state-directory DIR; --launcher-config FILE --clock-preview on|off; keys: H home, V vocabulary, A agenda, T advance preview minute, F flip, N next, B back, 1-4 rate, U undo, D day, G grace, R night, X blank, C clock untrusted, Escape quit. JSONL on stdin.\n";return 0;}if(i+1>=argc)throw std::runtime_error("SIMULATOR.ARGUMENT_REQUIRED");std::string value=argv[++i];if(option=="--scenario")scenario=value;else if(option=="--output"||option=="--screenshot")output=value;else if(option=="--exit-after-ms")exit_after=std::stol(value);else if(option=="--state-directory")state_directory=value;else if(option=="--launcher-config")application_catalog=value;else if(option=="--lexinet-config")lexinet_configuration=value;else if(option=="--clock-preview"){if(value!="on"&&value!="off")throw std::runtime_error("SIMULATOR.CLOCK_PREVIEW_INVALID");clock_preview=value=="on";}else throw std::runtime_error("SIMULATOR.ARGUMENT_UNKNOWN");}
 if(scenario.empty())throw std::runtime_error("SIMULATOR.SCENARIO_REQUIRED");
 bhp::simulator::PanelSimulation app(BHP_NATIVE_WINDOW,state_directory,lexinet_configuration,application_catalog);app.load(bhp::simulator::read_scenario(scenario));if(clock_preview)app.enable_preview_clock(true);emit(app.status());
#if BHP_NATIVE_WINDOW
 NativeInput input{&app,{}};SDL_AddEventWatch(observe_sdl,&input);
#endif
 const auto begin=std::chrono::steady_clock::now();std::string stdin_buffer;bool stdin_open=true;
 while(!app.quit&&(BHP_NATIVE_WINDOW||exit_after>0)){const auto before_service=app.render_generation;app.service();if(before_service!=app.render_generation)emit(app.status());
  while(auto contact=app.supervisor.take_contact()){app.tap(contact->first,contact->second);emit(app.status());}

#if BHP_NATIVE_WINDOW
  while(!input.keys.empty()){auto key=input.keys.front();input.keys.pop_front();const std::map<std::string,std::string> intents{{"H","shell.home"},{"V","shell.open_study"},{"A","shell.open_agenda"},{"F","study.flip"},{"Space","study.flip"},{"N","study.next"},{"B","study.back"},{"1","study.rate.again"},{"2","study.rate.hard"},{"3","study.rate.good"},{"4","study.rate.easy"},{"U","study.rating.undo"}};const std::map<std::string,std::string> lifecycle{{"D","day"},{"G","grace"},{"R","night"},{"X","blank"},{"C","clock_untrusted"}};
   if(intents.count(key))app.semantic_intent(intents.at(key),"touch");else if(lifecycle.count(key))app.event({{"type","lifecycle"},{"state",lifecycle.at(key)}});else if(key=="T")app.advance_preview_clock();else if(key=="Escape")app.quit=true;emit(app.status());}

#endif
  pollfd descriptor{STDIN_FILENO,POLLIN,0};if(stdin_open&&poll(&descriptor,1,0)>0&&(descriptor.revents&(POLLIN|POLLHUP))){char bytes[4096];auto count=read(STDIN_FILENO,bytes,sizeof bytes);if(count<=0)stdin_open=false;else stdin_buffer.append(bytes,count);if(stdin_buffer.size()>65536)throw std::runtime_error("SIMULATOR.STDIN_FRAME_TOO_LARGE");std::size_t pos;while((pos=stdin_buffer.find('\n'))!=std::string::npos){auto line=stdin_buffer.substr(0,pos);stdin_buffer.erase(0,pos+1);if(!line.empty()){app.event(bhp::simulator::Json::parse(line));emit(app.status());}}}
  if(exit_after>0&&std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-begin).count()>=exit_after)app.quit=true;
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
 }

#if BHP_NATIVE_WINDOW
 SDL_DelEventWatch(observe_sdl,&input);
#endif
 if(!output.empty()){app.display.write_png(output);emit({{"screenshot",output}});}return 0;
}catch(const std::exception &e){std::cerr<<bhp::simulator::Json{{"data",nullptr},{"error",{{"code","SIMULATOR.OPERATION_FAILED"},{"message",e.what()}}},{"meta",{{"simulation_not_hardware_evidence",true}}}}.dump()<<std::endl;return 1;}}
