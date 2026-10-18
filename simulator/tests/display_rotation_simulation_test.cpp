#include "simulator/panel_simulation.hpp"
#include "panel_ui/display_rotation.hpp"
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using bhp::simulator::PanelSimulation;
using house_panel::panel_ui::DisplayRotation;
void require(bool condition,const char *message){if(!condition)throw std::runtime_error(message);}
std::filesystem::path root(){return BHP_SIMULATOR_SOURCE_ROOT;}
void load_home(PanelSimulation &app){app.load(bhp::simulator::read_scenario((root()/"simulator/scenarios/home.json").string()));app.render();}
}

int main(){
 try{
  const auto launcher=(root()/"panel_ui/config/applications.json").string();
  // The raster the panel receives at 180 degrees is the rendered Home raster turned end for end.
  PanelSimulation rendered(false,{},{},launcher);load_home(rendered);
  const auto &pixels=rendered.display.pixels();
  const auto rotated=house_panel::panel_ui::rotate_raster(pixels,DisplayRotation::Degrees180);
  require(pixels.size()==1024u*600u&&rotated.size()==pixels.size(),"rotation keeps the 1024x600 raster size");
  for(std::size_t index=0;index<pixels.size();++index)if(rotated[index]!=pixels[pixels.size()-1-index])throw std::runtime_error("each panel pixel shows its opposite logical pixel");
  require(rotated!=pixels,"Home is not symmetric, so a missing rotation would be visible");
  require(house_panel::panel_ui::rotate_raster(rotated,DisplayRotation::Degrees180)==pixels,"two half turns restore the raster");
  // A touch on the rotated panel where a launcher tile appears opens that tile's app.
  const auto targets=rendered.display.hit_targets();
  const auto tile=std::find_if(targets.begin(),targets.end(),[](const auto &item){return item.intent_id.rfind("shell.open_",0)==0;});
  require(tile!=targets.end(),"Home shows a launcher tile");
  const int x=tile->x+tile->width/2,y=tile->y+tile->height/2;
  const auto logical=house_panel::panel_ui::logical_touch_point(1023-x,599-y,1024,600,DisplayRotation::Degrees180);
  require(logical.first==x&&logical.second==y,"the panel point under the drawn tile maps back to the tile");
  require(rendered.scene.surface_id=="home","the test starts on Home");
  rendered.tap(logical.first,logical.second);
  require(rendered.scene.surface_id!="home","the rotated tap opens the tile's app");
  return 0;
 }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
