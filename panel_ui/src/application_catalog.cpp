#include "panel_ui/application_catalog.hpp"
#include "panel_ui/panel_intent_registry.hpp"
#include <algorithm>
#include <regex>
#include <set>
#include <stdexcept>
namespace house_panel::panel_ui {
void validate_application_catalog(const ApplicationCatalog &catalog,const std::vector<std::string> &surfaces){
 if(catalog.columns<1||catalog.columns>4||catalog.rows<1||catalog.rows>2||catalog.applications.size()>64)
  throw std::runtime_error("PANEL_UI.APP_CATALOG_BOUNDS_INVALID");
 PanelIntentRegistry registry;std::set<std::string> ids;
 for(const auto &app:catalog.applications){
  if(!std::regex_match(app.application_id,std::regex("^[a-z][a-z0-9_-]{0,47}$"))||!ids.insert(app.application_id).second)
   throw std::runtime_error("PANEL_UI.APP_CATALOG_ID_INVALID");
  if(app.title.empty()||app.title.size()>48||app.subtitle.size()>48||app.title.find_first_of("\r\n\t")!=std::string::npos||app.subtitle.find_first_of("\r\n\t")!=std::string::npos)
   throw std::runtime_error("PANEL_UI.APP_CATALOG_LABEL_INVALID");
  if((app.icon!="book"&&app.icon!="calendar"&&app.icon!="monogram")||app.icon_color>0xffffffu||
    (app.icon=="monogram"?!std::regex_match(app.monogram,std::regex("^[A-Z0-9]{1,2}$")):!app.monogram.empty()))
   throw std::runtime_error("PANEL_UI.APP_CATALOG_ICON_INVALID");
  const auto *intent=registry.find(app.launch_intent);
  if(!intent||!intent->enabled||!intent->allow_touch||intent->effect_class!=IntentEffectClass::Navigation||intent->navigation_surface_id.empty()||intent->navigation_surface_id=="home"||
    std::find(surfaces.begin(),surfaces.end(),intent->navigation_surface_id)==surfaces.end())
   throw std::runtime_error("PANEL_UI.APP_CATALOG_ROUTE_INVALID");
 }
}
ApplicationGridPage application_grid_page(const ApplicationCatalog &catalog,std::size_t page){
 if(catalog.columns<1||catalog.columns>4||catalog.rows<1||catalog.rows>2||catalog.applications.size()>64)
  throw std::runtime_error("PANEL_UI.APP_CATALOG_BOUNDS_INVALID");
 std::vector<std::size_t> enabled;
 for(std::size_t i=0;i<catalog.applications.size();++i)if(catalog.applications[i].enabled)enabled.push_back(i);
 const auto capacity=catalog.columns*catalog.rows;
 ApplicationGridPage result;result.page_count=std::max<std::size_t>(1,(enabled.size()+capacity-1)/capacity);
 if(page>=result.page_count)throw std::runtime_error("PANEL_UI.APP_CATALOG_PAGE_INVALID");
 const auto first=page*capacity,count=std::min(capacity,enabled.size()-first);
 if(!count)return result;
 // Preserve the approved roomy one-row composition. Multiple rows/pages use a
 // compact grid; incomplete last pages retain the preceding page's geometry.
 const auto columns=std::min(catalog.columns,enabled.size());
 const auto rows=std::min(catalog.rows,(enabled.size()+columns-1)/columns);
 const bool paged=result.page_count>1;
 const int slot_width=864/static_cast<int>(columns);
 const int tile_width=std::min(256,slot_width-16);
 const int tile_height=rows==1?(paged?272:300):(paged?140:156);
 const int top=rows==1?(paged?72:88):72;
 const int icon_size=rows==1?144:(paged?64:80);
 for(std::size_t i=0;i<count;++i){
  const int x=40+static_cast<int>(i%columns)*slot_width+(slot_width-tile_width)/2;
  const int y=top+static_cast<int>(i/columns)*tile_height;
  result.tiles.push_back({enabled[first+i],x,y,tile_width,tile_height,icon_size,rows==1?16:0,
    rows==1?178:icon_size+4,rows==1?226:icon_size+45});
 }
 return result;
}
}
