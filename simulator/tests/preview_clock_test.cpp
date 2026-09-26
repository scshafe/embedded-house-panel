#include "simulator/preview_clock.hpp"
#include "fixtures/preview_clock_cases.hpp"
#include <iostream>
#include <stdexcept>
using bhp::simulator::PreviewClock;
namespace fixture=bhp::simulator::clock_fixtures;
static void require(bool value){if(!value)throw std::runtime_error("preview clock assertion failed");}
int main(){
 for(const auto *bad:fixture::invalid_texts)require(!PreviewClock::valid_text(bad));
 require(PreviewClock::next_minute(fixture::last_minute)==fixture::midnight);
 PreviewClock clock;clock.reset(fixture::before_roll,1000,true);
 require(clock.sample(60999).text==fixture::before_roll);
 auto first=clock.sample(61000);require(first.text==fixture::after_roll&&first.previous_text==fixture::before_roll&&first.transition_milli==0);
 for(const auto elapsed:fixture::transition_samples){
  const auto frame=clock.sample(61000+elapsed);require(frame.transition_milli==elapsed*1000/400);
  require((elapsed==400)==frame.previous_text.empty());
 }
 clock.reset(fixture::last_minute,0,true);require(clock.sample(60000).text==fixture::midnight);
 clock.sample(60100,false);auto returned=clock.sample(60200,true);require(returned.previous_text.empty());
 clock.reset(fixture::before_roll,0,false);clock.change_to(fixture::after_roll,10);require(clock.sample(210).transition_milli==500);
 clock.change_to(fixture::interruption,220);require(clock.sample(220).previous_text==fixture::after_roll);
 clock.cancel_transition();require(clock.frame().previous_text.empty()&&clock.frame().text==fixture::interruption);
 clock.reset(fixture::before_roll,0,true);require(clock.sample(180000).text==fixture::after_three_minutes);
 clock.reset(fixture::before_roll,0,false);require(clock.sample(180000).text==fixture::before_roll);
 bool rejected=false;try{clock.sample(1);}catch(const std::runtime_error &e){rejected=std::string(e.what())=="SIMULATOR.CLOCK_MONOTONIC_REVERSED";}require(rejected);
 std::cout<<"{\"test\":\"preview_clock\",\"passed\":true,\"evidence\":\"simulation_not_hardware_evidence\"}\n";
}
