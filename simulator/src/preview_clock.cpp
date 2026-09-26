#include "simulator/preview_clock.hpp"
#include <algorithm>
#include <stdexcept>

namespace bhp::simulator {
namespace {
unsigned minute_of_day(const std::string &text) {
 if(!PreviewClock::valid_text(text))throw std::runtime_error("SIMULATOR.CLOCK_TEXT_INVALID");
 return static_cast<unsigned>((text[0]-'0')*600+(text[1]-'0')*60+(text[3]-'0')*10+text[4]-'0');
}
std::string clock_text(unsigned minute) {
 const auto hour=minute/60;minute%=60;
 std::string text="00:00";
 text[0]=static_cast<char>('0'+hour/10);text[1]=static_cast<char>('0'+hour%10);
 text[3]=static_cast<char>('0'+minute/10);text[4]=static_cast<char>('0'+minute%10);return text;
}
}
bool PreviewClock::valid_text(const std::string &text) noexcept {
 return text.size()==5&&text[2]==':'&&text[0]>='0'&&text[0]<='2'&&text[1]>='0'&&text[1]<='9'
  &&(text[0]!='2'||text[1]<='3')&&text[3]>='0'&&text[3]<='5'&&text[4]>='0'&&text[4]<='9';
}
std::string PreviewClock::next_minute(const std::string &text){return clock_text((minute_of_day(text)+1)%1440);}
void PreviewClock::reset(const std::string &text,std::uint64_t now,bool ticking){
 origin_minute_=minute_of_day(text);origin_ms_=last_ms_=transition_ms_=now;
 frame_={text,{},1000};ticking_=ticking;visible_=true;
}
void PreviewClock::change_to(const std::string &text,std::uint64_t now){
 const auto minute=minute_of_day(text);
 if(now<last_ms_)throw std::runtime_error("SIMULATOR.CLOCK_MONOTONIC_REVERSED");
 if(text!=frame_.text){frame_.previous_text=frame_.text;frame_.text=text;frame_.transition_milli=0;transition_ms_=now;}
 origin_minute_=minute;origin_ms_=last_ms_=now;
}
PreviewClockFrame PreviewClock::sample(std::uint64_t now,bool visible){
 if(now<last_ms_)throw std::runtime_error("SIMULATOR.CLOCK_MONOTONIC_REVERSED");
 last_ms_=now;
 if(ticking_){
  const auto minute=(origin_minute_+(now-origin_ms_)/60000)%1440;
  const auto text=clock_text(static_cast<unsigned>(minute));
  if(text!=frame_.text){frame_.previous_text=frame_.text;frame_.text=text;frame_.transition_milli=0;transition_ms_=now;}
 }
 if(!visible||!visible_)cancel_transition();
 else if(!frame_.previous_text.empty()){
  const auto elapsed=std::min(now-transition_ms_,transition_duration_ms);
  frame_.transition_milli=static_cast<std::uint16_t>(elapsed*1000/transition_duration_ms);
  if(elapsed==transition_duration_ms)frame_.previous_text.clear();
 }
 visible_=visible;return frame_;
}
void PreviewClock::cancel_transition() noexcept {frame_.previous_text.clear();frame_.transition_milli=1000;}
}
