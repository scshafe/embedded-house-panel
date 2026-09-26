#include "simulator/panel_simulation.hpp"
#include "simulator/application_catalog_configuration.hpp"
#include "panel_ui/application_catalog.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
using namespace house_panel::panel_ui;
using bhp::simulator::PanelSimulation;
using bhp::simulator::Json;
namespace {
void require(bool condition,const char *message){if(!condition)throw std::runtime_error(message);}
const auto root=std::filesystem::path(BHP_SIMULATOR_SOURCE_ROOT);
const auto fixtures=root/"simulator/tests/fixtures/application_grid";
void tap(PanelSimulation &app,const std::string &id){
 const auto targets=app.display.hit_targets();
 const auto found=std::find_if(targets.begin(),targets.end(),[&](const auto &t){return t.intent_id==id;});
 require(found!=targets.end(),"visible route must have a hit target");
 app.tap(found->x+found->width/2,found->y+found->height/2);
}
void check_layout(const ApplicationCatalog &catalog){
 std::set<std::size_t> seen;
 const auto pages=application_grid_page(catalog,0).page_count;
 for(std::size_t page=0;page<pages;++page){
  const auto layout=application_grid_page(catalog,page);
  for(std::size_t i=0;i<layout.tiles.size();++i){
   const auto &a=layout.tiles[i];
   require(seen.insert(a.application_index).second,"an app occurs on exactly one page");
   require(a.x>=0&&a.y>=68&&a.x+a.width<=944&&a.y+a.height<=432,"all tiles fit the content area below its heading");
   require(a.width>=64&&a.height>=64,"grid preserves touch target size");
   require(a.icon_size<=a.width&&a.subtitle_y+31<=a.height,"icon and labels fit their tile");
   if(pages>1)require(a.y+a.height<=352,"paged tiles cannot cover pager controls");
   for(std::size_t n=i+1;n<layout.tiles.size();++n){const auto &b=layout.tiles[n];require(a.x+a.width<=b.x||b.x+b.width<=a.x||a.y+a.height<=b.y||b.y+b.height<=a.y,"app hit regions cannot overlap");}
  }
 }
 require(seen.size()==static_cast<std::size_t>(std::count_if(catalog.applications.begin(),catalog.applications.end(),[](const auto &a){return a.enabled;})),"every enabled app appears in the grid");
 bool rejected=false;try{(void)application_grid_page(catalog,pages);}catch(const std::runtime_error &){rejected=true;}require(rejected,"out-of-range page fails explicitly");
}
void layouts_and_validation(){
 const auto fixture=bhp::simulator::read_application_catalog((fixtures/"seven_apps.json").string());
 // Explore all supported grid shapes, the capacity boundary and the catalog limit.
 for(std::size_t columns=1;columns<=4;++columns)for(std::size_t rows=1;rows<=2;++rows)for(const std::size_t count:{0,1,2,6,7,8,9,64}){
  auto catalog=fixture;catalog.columns=columns;catalog.rows=rows;catalog.applications.clear();
  for(std::size_t i=0;i<count;++i){auto app=fixture.applications[i%fixture.applications.size()];app.application_id+="-"+std::to_string(i);catalog.applications.push_back(app);}
  validate_application_catalog(catalog,{"study","agenda"});check_layout(catalog);
 }
 std::ifstream input(fixtures/"invalid_cases.json");const auto invalid=Json::parse(input);
 for(const auto &item:invalid){bool rejected=false;
  try{(void)bhp::simulator::read_application_catalog((fixtures/item.at("file").get<std::string>()).string());}
  catch(const std::runtime_error &error){rejected=std::string(error.what()).find(item.at("code").get<std::string>())!=std::string::npos;}
  require(rejected,"invalid catalogs retain a content-free typed rejection");
 }
}
void render_all_grid_shapes(){
 PanelSimulation app(false,{}, {},(fixtures/"seven_apps.json").string());
 app.load(bhp::simulator::read_scenario((root/"simulator/scenarios/home.json").string()));
 for(std::size_t columns=1;columns<=4;++columns)for(std::size_t rows=1;rows<=2;++rows){
  auto scene=app.scene;scene.applications.columns=columns;scene.applications.rows=rows;
  const auto count=application_grid_page(scene.applications,0).page_count;
  for(std::size_t page=0;page<count;++page){scene.home_page=page;require(app.display.render(scene)>0,"all grid shapes fit pinned fonts and supported icons");}
 }
}
void navigation_and_lifecycle(){
 PanelSimulation app(false,{}, {},(fixtures/"seven_apps.json").string());
 app.load(bhp::simulator::read_scenario((root/"simulator/scenarios/home.json").string()));
 require(app.scene.home_page==0&&app.status().at("home_page_count")==2,"seven apps become two pages");
 require(app.display.hit_targets().size()==8,"first page has six apps, Home and Next");
 const auto initial=app.render_generation;tap(app,"shell.apps_next");
 require(app.scene.home_page==1&&app.render_generation>initial,"Next renders the second page");
 require(app.display.hit_targets().size()==3,"last page has one app, Home and Previous");
 tap(app,"shell.open_study");require(app.scene.surface_id=="study","last-page app launches through the shell");
 app.semantic_intent("study.flip","touch");tap(app,"shell.home");
 require(app.scene.home_page==1,"Home retains the last launcher page");
 tap(app,"shell.open_study");require(app.scene.study_state=="back","paging and navigation preserve study state");
 tap(app,"shell.home");tap(app,"shell.apps_previous");require(app.scene.home_page==0,"Previous returns to the first page");
 app.semantic_intent("shell.apps_next","voice");require(app.scene.home_page==0,"paging is never a spoken command");
 for(const auto *mode:{"night","blank","clock_untrusted"}){
  app.event({{"type","lifecycle"},{"state",mode}});app.semantic_intent("shell.apps_next","host_fixture");
  require(app.scene.home_page==0&&app.display.hit_targets().empty(),"non-day modes cannot page or launch");
 }
 app.event({{"type","lifecycle"},{"state","day"}});app.invalidate_lifecycle();app.semantic_intent("shell.apps_next","touch");
 require(app.scene.home_page==0&&app.display.hit_targets().empty(),"lost lifecycle cannot page or launch");
}
void visibility_and_empty_catalogs(){
 for(const auto *name:{"reordered_hidden.json","empty.json"}){
  PanelSimulation app(false,{}, {},(fixtures/name).string());
  app.load(bhp::simulator::read_scenario((root/"simulator/scenarios/home.json").string()));
  const bool empty=std::string(name)=="empty.json";
  require(app.display.hit_targets().size()==(empty?1u:2u),"only enabled app routes have tiles");
  app.semantic_intent("shell.open_study","touch");require(app.scene.surface_id=="home","hidden/unconfigured routes cannot launch from Home");
  if(!empty){tap(app,"shell.open_agenda");require(app.scene.surface_id=="agenda","reordered visible app reaches canonical destination");}
 }
}
}
int main(){try{layouts_and_validation();render_all_grid_shapes();navigation_and_lifecycle();visibility_and_empty_catalogs();std::cout<<"{\"test\":\"panel_application_grid\",\"passed\":true}\n";return 0;}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
