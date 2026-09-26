#include "simulator/application_catalog_configuration.hpp"
#include <nlohmann/json.hpp>
#include <panel_fonts/glyph_coverage.hpp>
#include <fstream>
#include <regex>
#include <set>
#include <stdexcept>
namespace bhp::simulator {
namespace {
using Json=nlohmann::json;
void fields(const Json &object,const std::set<std::string> &required,const std::set<std::string> &optional){
 if(!object.is_object())throw std::runtime_error("SIMULATOR.APP_CATALOG_SCHEMA_INVALID");
 for(const auto &key:required)if(!object.contains(key))throw std::runtime_error("SIMULATOR.APP_CATALOG_FIELD_MISSING");
 for(auto it=object.begin();it!=object.end();++it)if(!required.count(it.key())&&!optional.count(it.key()))throw std::runtime_error("SIMULATOR.APP_CATALOG_FIELD_UNKNOWN");
}
}
house_panel::panel_ui::ApplicationCatalog read_application_catalog(const std::string &path){
 std::ifstream file(path);if(!file)throw std::runtime_error("SIMULATOR.APP_CATALOG_READ_FAILED");
 std::string bytes(65537,'\0');file.read(bytes.data(),static_cast<std::streamsize>(bytes.size()));bytes.resize(static_cast<std::size_t>(file.gcount()));
 if(bytes.size()>65536)throw std::runtime_error("SIMULATOR.APP_CATALOG_TOO_LARGE");
 house_panel::panel_ui::ApplicationCatalog catalog;
 try{
  const auto j=Json::parse(bytes);fields(j,{"schema_version","columns","rows","applications"},{});
  if(!j.at("schema_version").is_number_integer()||j.at("schema_version")!=1||!j.at("columns").is_number_integer()||j.at("columns")<1||j.at("columns")>4||!j.at("rows").is_number_integer()||j.at("rows")<1||j.at("rows")>2||!j.at("applications").is_array()||j.at("applications").size()>64)
   throw std::runtime_error("SIMULATOR.APP_CATALOG_SCHEMA_INVALID");
  catalog.columns=j.at("columns");catalog.rows=j.at("rows");
  for(const auto &value:j.at("applications")){
   fields(value,{"application_id","title","launch_intent","icon","icon_color"},{"subtitle","monogram","enabled","preview"});
   const auto color=value.at("icon_color").get<std::string>();
   if(!std::regex_match(color,std::regex("^[0-9a-fA-F]{6}$")))throw std::runtime_error("SIMULATOR.APP_CATALOG_COLOR_INVALID");
   house_panel::panel_ui::ApplicationDescriptor app{value.at("application_id"),value.at("title"),value.value("subtitle",""),value.at("launch_intent"),value.at("icon"),value.value("monogram",""),static_cast<std::uint32_t>(std::stoul(color,nullptr,16)),value.value("enabled",true),value.value("preview",false)};
   for(const auto *text:{&app.title,&app.subtitle})if(!bhp::panel_fonts::validate_glyph_coverage(*text).supported)throw std::runtime_error("SIMULATOR.APP_CATALOG_GLYPH_UNSUPPORTED");
   catalog.applications.push_back(std::move(app));
  }
 }catch(const Json::exception &){throw std::runtime_error("SIMULATOR.APP_CATALOG_SCHEMA_INVALID");}
 // @extension-point simulation_feature_surfaces -- register actual new feature
 // implementations here alongside their ShellController contract and renderer.
 house_panel::panel_ui::validate_application_catalog(catalog,{"study","agenda"});
 return catalog;
}
}
