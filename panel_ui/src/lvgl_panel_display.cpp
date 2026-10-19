#include "panel_ui/lvgl_panel_display.hpp"
#include "panel_ui/panel_app_descriptor.hpp"
#include "vocabulary_panel/gui/lvgl/lvgl_flash_card_renderer.h"
#include "vocabulary_panel/gui/lvgl/vocabulary_surface_layout.h"
#include <panel_fonts/panel_fonts.h>
#include <src/display/lv_display_private.h>
#include <png.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <utility>

namespace house_panel::panel_ui {
namespace {
constexpr int width=1024,height=600;
namespace layout = vocabulary_panel::gui::lvgl::layout;
using bhp::panel_mode_protocol::DisplayMode;
using bhp::panel_mode_protocol::SolarState;
using bhp::panel_mode_protocol::SystemState;
void label(lv_obj_t *parent, const std::string &text, int x, int y, int w, int maximum_height,
           const lv_font_t *font, std::uint32_t color, lv_text_align_t alignment = LV_TEXT_ALIGN_LEFT) {
 auto *o = lv_label_create(parent);
 lv_label_set_text(o, text.c_str());
 lv_label_set_long_mode(o, LV_LABEL_LONG_MODE_WRAP);
 lv_obj_set_pos(o, x, y);
 lv_obj_set_width(o, w);
 lv_obj_set_style_text_font(o, font, 0);
 lv_obj_set_style_text_color(o, lv_color_hex(color), 0);
 lv_obj_set_style_text_align(o, alignment, 0);
 lv_obj_set_style_text_line_space(o, 0, 0);
 lv_obj_update_layout(o);
 if(lv_obj_get_height(o) > maximum_height)
  throw std::runtime_error("PANEL_UI.TEXT_LAYOUT_OVERFLOW:x="+std::to_string(x)+",y="+std::to_string(y)+",width="+std::to_string(w)+",height="+std::to_string(lv_obj_get_height(o))+",limit="+std::to_string(maximum_height));
}
lv_obj_t *box(lv_obj_t *parent,int x,int y,int w,int h,std::uint32_t color,int radius=0) {
 auto *o=lv_obj_create(parent);lv_obj_remove_style_all(o);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);
 lv_obj_set_style_bg_color(o,lv_color_hex(color),0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);
 lv_obj_set_style_radius(o,radius,0);lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE);return o;
}
bool clock_available(const PanelScene &s) {
 // Clock provenance is independent of study data. No civil-time provider is
 // inferred from live study transport or an unavailable source.
 return s.clock_source == "fixture" && !s.presentation_failure && !s.fixture_clock_untrusted &&
        s.lifecycle.display_mode != DisplayMode::Suspended &&
        s.lifecycle.display_mode != DisplayMode::Diagnostic &&
        s.lifecycle.solar_state != SolarState::Unknown &&
        (s.lifecycle.display_mode != DisplayMode::NightRed || s.lifecycle.solar_state == SolarState::Night) &&
        s.lifecycle.system_state == SystemState::Active;
}
bool clock_text_valid(const std::string &text) {
 if(text.size() != 5 || text[2] != ':') return false;
 for(const auto index : {0, 1, 3, 4}) if(text[index] < '0' || text[index] > '9') return false;
 return (text[0] - '0') * 10 + text[1] - '0' < 24 &&
        (text[3] - '0') * 10 + text[4] - '0' < 60;
}
std::uint32_t temperature_color(int celsius) {
 // Both forecast roles use the same actual-temperature scale. A day's high can
 // be cold, and its low can be hot; the labels never determine their colors.
 if(celsius <= 0) return 0x7faee8;
 if(celsius <= 10) return 0x79c5d6;
 if(celsius <= 20) return 0x9bc996;
 if(celsius <= 30) return 0xe9c480;
 return 0xec8b78;
}
std::string temperature_text(int celsius, const std::string &unit) {
 const auto value = unit == "C" ? celsius : static_cast<int>(std::lround(celsius * 1.8 + 32));
 return std::to_string(value) + "°" + unit;
}
void draw_clock(lv_obj_t *screen, const PanelScene &s, const lv_font_t *font, int digit_width, int center_y, std::uint32_t color) {
 const int cell_height = lv_font_get_line_height(font);
 lv_point_t colon_size{};
 lv_text_get_size(&colon_size, ":", font, 0, 0, width, LV_TEXT_FLAG_NONE);
 const int total_width = digit_width * 4 + colon_size.x;
 int x = (width - total_width) / 2;
 const int y = center_y - cell_height / 2;
 const bool transition = s.clock_transition_milli < 1000;
 const double progress = s.clock_transition_milli / 1000.0;
 const int offset = static_cast<int>(std::lround(cell_height * progress * progress * (3 - 2 * progress)));
 for(std::size_t index = 0; index < s.clock_text.size(); ++index) {
  const int cell_width = index == 2 ? colon_size.x : digit_width;
  auto *cell = lv_obj_create(screen);
  lv_obj_remove_style_all(cell);
  lv_obj_set_pos(cell, x, y);
  lv_obj_set_size(cell, cell_width, cell_height);
  lv_obj_remove_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(cell, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  // Each cell clips its outgoing/upward and entering/below digit. The colon
  // and unchanged digits are static, and no LVGL timer owns transition state.
  if(transition && index != 2 && s.clock_text[index] != s.clock_previous_text[index]) {
   label(cell, std::string(1, s.clock_previous_text[index]), 0, -offset, cell_width,
         cell_height, font, color, LV_TEXT_ALIGN_CENTER);
   label(cell, std::string(1, s.clock_text[index]), 0, cell_height - offset, cell_width,
         cell_height, font, color, LV_TEXT_ALIGN_CENTER);
  } else label(cell, std::string(1, s.clock_text[index]), 0, 0, cell_width,
               cell_height, font, color, LV_TEXT_ALIGN_CENTER);
  x += cell_width;
 }
}
void home_icon(lv_obj_t *parent) {
 static const lv_point_precise_t roof_points[]{{0, 17}, {17, 0}, {34, 17}};
 auto *roof = lv_line_create(parent);
 lv_line_set_points(roof, roof_points, 3);
 lv_obj_set_pos(roof, 17, 14);
 lv_obj_set_style_line_color(roof, lv_color_hex(0xdbe5df), 0);
 lv_obj_set_style_line_width(roof, 4, 0);
 box(parent, 22, 31, 24, 22, 0xdbe5df, 1);
 box(parent, 30, 39, 8, 14, 0x293f43);
}
void launcher_icon(lv_obj_t *parent, const ApplicationDescriptor &app, const ApplicationTile &tile) {
 const auto scale=[&](int value){return static_cast<int>(std::lround(value*tile.icon_size/144.0));};
 auto *icon=box(parent,(tile.width-tile.icon_size)/2,tile.icon_y,tile.icon_size,tile.icon_size,app.icon_color,scale(30));
 const auto shape=[&](int x,int y,int w,int h,std::uint32_t color,int radius=0){
  box(icon,scale(x),scale(y),std::max(1,scale(w)),std::max(1,scale(h)),color,scale(radius));
 };
 if(app.icon=="calendar") {
  shape(27,36,90,82,0xf4f0e8,7);shape(27,48,90,12,0xe9c480);
  shape(44,26,9,24,0xf4f0e8,4);shape(91,26,9,24,0xf4f0e8,4);
  for(int row=0;row<2;++row)for(int col=0;col<3;++col)shape(42+col*24,73+row*22,12,10,app.icon_color,2);
 } else if(app.icon=="book") {
  shape(23,34,46,82,0xf4f0e8,7);shape(75,34,46,82,0xf4f0e8,7);
  for(int row=0;row<3;++row){shape(32,51+row*17,27,3,app.icon_color,1);shape(85,51+row*17,27,3,app.icon_color,1);}
 } else {
  const auto *font=tile.icon_size>=100?&bhp_font_40:&bhp_font_28;
  const auto line_height=lv_font_get_line_height(font);
  label(icon,app.monogram,0,(tile.icon_size-line_height)/2,tile.icon_size,line_height,font,0xf4f0e8,LV_TEXT_ALIGN_CENTER);
 }
}
std::string acknowledgement_text(const std::string &value) {
 if(value.empty() || value == "Action accepted" || value == "Already handled") return {};
 if(value == "STUDY.RATING_UNAVAILABLE") return "Rating unavailable for this card.";
 if(value == "STUDY.UNDO_UNAVAILABLE") return "Nothing to undo.";
 if(value == "STUDY.TRACKING_PAUSED") return "Study is paused.";
 if(value == "Recognition busy") return "Listening…";
 if(value == "Voice abstained - no action") return "Did not catch that. Try again.";
 if(value == "Voice unavailable - touch remains ready" || value == "Use the fake recognition input")
  return "Voice unavailable. Use touch.";
 if(value.rfind("LEXINET.", 0) == 0) return "Lexinet is unavailable.";
 if(value.find('.') != std::string::npos && value.find(' ') == std::string::npos)
  return "That action is unavailable.";
 return value;
}
std::string status_text(const PanelScene &s) {
 std::string status;
 const auto append = [&status](const std::string &part) {
  if(part.empty()) return;
  if(!status.empty()) status += " · ";
  status += part;
 };
 if(s.offline) append("Offline");
 if(s.stale || (s.surface_id == "agenda" && s.agenda_state == "stale")) append("May be out of date");
 // Pending and rejected reviews remain visible independently of transient
 // acknowledgements. Connectivity and freshness never replace delivery truth.
 if(s.surface_id == "study") append(s.delivery_status);
 return status;
}
std::vector<std::string> scene_text(const PanelScene &s) {
 if(s.presentation_failure || s.lifecycle.display_mode == DisplayMode::Suspended) return {};
 if(s.lifecycle.display_mode == DisplayMode::NightRed) {
  if(!clock_available(s)) return {};
  return s.clock_transition_milli < 1000 ? std::vector<std::string>{s.clock_text, s.clock_previous_text} :
                                         std::vector<std::string>{s.clock_text};
 }
 if(s.lifecycle.display_mode == DisplayMode::Diagnostic || s.fixture_clock_untrusted) return {};
 std::vector<std::string> result{status_text(s), acknowledgement_text(s.acknowledgement)};
 if(clock_available(s)) result.push_back(s.clock_text);
 if(s.surface_id=="home")for(const auto &tile:application_grid_page(s.applications,s.home_page).tiles){
  const auto &app=s.applications.applications.at(tile.application_index);
  result.push_back(app.title);result.push_back(app.subtitle);result.push_back(app.monogram);
 }
 if(s.surface_id == "study" && (s.study_state == "front" || s.study_state == "back")) {
  result.push_back(s.card_word);
  if(s.study_state == "back") { result.push_back(s.card_definition); result.push_back(s.card_example); }
 }
 return result;
}
}
LvglPanelDisplay::LvglPanelDisplay(bool sdl_window):pixels_(width*height),draw_buffer_(width*height) {
 if(lv_is_initialized()) throw std::runtime_error("PANEL_UI.LVGL_ALREADY_INITIALIZED");
 for(const auto &app : panel_app_descriptors())
  if(find_surface_renderer(app.surface_id) == nullptr) throw std::runtime_error("PANEL_UI.SURFACE_RENDERER_MISSING");
 auto policy=HeadlessRenderer::create({8192,4096,true});
 if(!policy) throw std::runtime_error(std::string(policy.error().code_string()));
 text_renderer_=std::move(policy).value();lv_init();
 if(sdl_window) {
#if LV_USE_SDL
  display_=lv_sdl_window_create(width,height);
  if(display_) {lv_sdl_window_set_title(display_,"House panel | SIMULATION - not hardware evidence");lv_sdl_window_set_resizeable(display_,false);}
#else
  throw std::runtime_error("PANEL_UI.SDL_NOT_BUILT");
#endif
 } else {
  display_=lv_display_create(width,height);
  lv_display_set_color_format(display_,LV_COLOR_FORMAT_XRGB8888);
  lv_display_set_buffers(display_,draw_buffer_.data(),nullptr,draw_buffer_.size()*4,LV_DISPLAY_RENDER_MODE_FULL);
 }
 if(!display_) throw std::runtime_error("PANEL_UI.DISPLAY_CREATE_FAILED");
 driver_flush_=display_->flush_cb;
 lv_display_set_user_data(display_,this);lv_display_set_flush_cb(display_,flush);
 lv_display_set_default(display_);
}
LvglPanelDisplay::~LvglPanelDisplay(){lv_deinit();}
void LvglPanelDisplay::flush(lv_display_t *d,const lv_area_t *area,std::uint8_t *map) {
 auto *self=static_cast<LvglPanelDisplay*>(lv_display_get_user_data(d));
 const auto *source=reinterpret_cast<const std::uint32_t*>(map);
 for(int y=area->y1;y<=area->y2;++y)
  std::copy(source+y*width+area->x1,source+y*width+area->x2+1,self->pixels_.begin()+y*width+area->x1);
 if(self->driver_flush_) self->driver_flush_(d,area,map); else lv_display_flush_ready(d);
}
std::uint64_t LvglPanelDisplay::render(const PanelScene &s) {
 auto validated=text_renderer_->render({s.lifecycle.display_mode,ShellTransitionState::Stable,s.surface_id,scene_text(s)});
 if(!validated) throw std::runtime_error(std::string(validated.error().code_string()));
 for(const auto &text : validated.value().visible_text_runs)
  if(!bhp::panel_fonts::validate_glyph_coverage(text, s.lifecycle.display_mode == DisplayMode::NightRed ?
       bhp::panel_fonts::FontSubset::Clock : bhp::panel_fonts::FontSubset::Latin).supported)
   throw std::runtime_error("PANEL_UI.FONT_GLYPH_UNSUPPORTED");
 if(clock_available(s) && (!clock_text_valid(s.clock_text) || s.clock_transition_milli > 1000 ||
    (s.clock_transition_milli < 1000 && !clock_text_valid(s.clock_previous_text))))
  throw std::runtime_error("PANEL_UI.CLOCK_FRAME_INVALID");
 draw_scene(s);lv_obj_invalidate(lv_display_get_screen_active(display_));lv_refr_now(display_);
 return validated.value().render_generation;
}
void LvglPanelDisplay::service(){lv_timer_handler();}
void LvglPanelDisplay::write_png(const std::string &path) const {
 std::filesystem::path target(path);if(target.has_parent_path())std::filesystem::create_directories(target.parent_path());
 std::vector<std::uint8_t> rgb(width*height*3);
 for(std::size_t i=0;i<pixels_.size();++i){rgb[i*3]=pixels_[i]>>16;rgb[i*3+1]=pixels_[i]>>8;rgb[i*3+2]=pixels_[i];}
 png_image image{};image.version=PNG_IMAGE_VERSION;image.width=width;image.height=height;image.format=PNG_FORMAT_RGB;
 if(!png_image_write_to_file(&image,path.c_str(),0,rgb.data(),0,nullptr))throw std::runtime_error("PANEL_UI.PNG_WRITE_FAILED");
}
void LvglPanelDisplay::draw_scene(const PanelScene &s) {
 auto *screen = lv_display_get_screen_active(display_);
 lv_obj_clean(screen);
 lv_obj_remove_style_all(screen);
 lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
 lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
 hit_targets_.clear();
 const auto mode = s.lifecycle.display_mode;
 if(s.presentation_failure || mode == DisplayMode::Suspended || mode == DisplayMode::NightRed) {
  lv_obj_set_style_bg_color(screen, lv_color_hex(0), 0);
  if(mode == DisplayMode::NightRed && !s.presentation_failure && clock_available(s)) {
   lv_obj_set_style_bg_color(screen, lv_color_hex(s.night_red_rgb888), 0);
   draw_clock(screen, s, &bhp_clock_font_192, 128, height / 2, 0);
  }
  return;
 }
 lv_obj_set_style_bg_color(screen, lv_color_hex(0x101c20), 0);
 const bool diagnostic = mode == DisplayMode::Diagnostic || s.fixture_clock_untrusted;
 if(!diagnostic) {
  auto *home = box(screen, 40, 8, 160, 64, 0x293f43, 12);
  home_icon(home);
  label(home, "Home", 60, 13, 96, 37, &bhp_font_28, 0xdbe5df);
  hit_targets_.push_back({40, 8, 160, 64, "shell.home"});
  const bool preview = s.fixture_data || s.clock_source == "fixture" || s.weather_fixture;
  if(preview) label(screen, "Preview", 208, s.weather_stale ? 4 : 22, 192, 31, &bhp_font_24, 0x8db5a7);
  if(s.weather_stale) label(screen, "Weather stale", 208, preview ? 37 : 22, 192, 31, &bhp_font_24, 0xe9c480);
  if(clock_available(s)) draw_clock(screen, s, &bhp_font_48, 32, 33, 0xf4f0e8);
  else label(screen, "—", 360, 6, 304, 62, &bhp_font_48, 0xf4f0e8, LV_TEXT_ALIGN_CENTER);
  for(int index = 0; index < 2; ++index) {
   const int x = index == 0 ? 712 : 856;
   const int temperature = index == 0 ? s.weather_high_celsius : s.weather_low_celsius;
   label(screen, index == 0 ? "High" : "Low", x, 2, 128, 31, &bhp_font_24, 0xaab8b7, LV_TEXT_ALIGN_RIGHT);
   label(screen, s.weather_available ? temperature_text(temperature, s.weather_temperature_unit) : "—",
         x, 33, 128, 37, &bhp_font_28, s.weather_available ? temperature_color(temperature) : 0xaab8b7,
         LV_TEXT_ALIGN_RIGHT);
  }
 } else if(s.fixture_data || s.clock_source == "fixture")
  label(screen, "Preview", 40, 22, 650, 31, &bhp_font_24, 0x8db5a7);
 auto *content = box(screen, layout::content_x, layout::content_y, layout::content_width,
                     layout::content_height, 0x1c2d31, 18);
 if(diagnostic) {
  label(content, "Time unavailable", 24, 60, 896, 104, &bhp_font_40, 0xf4f0e8);
  label(content, "Waiting for the clock to be ready.", 24, 184, 896, 111, &bhp_font_28, 0xaab8b7);
  return;
 }
 const auto status = status_text(s);
 if(!status.empty()) label(screen, status, 40, 72, 650, 31, &bhp_font_24, 0xe9c480);
 const auto acknowledgement = acknowledgement_text(s.acknowledgement);
 if(!acknowledgement.empty()) label(screen, acknowledgement, 40, 107, 650, 31, &bhp_font_24, 0xe9c480);
 const auto *renderer = find_surface_renderer(s.surface_id);
 if(renderer == nullptr) throw std::runtime_error("PANEL_UI.SURFACE_RENDERER_MISSING");
 (this->*renderer->draw)(screen, content, s);
 if(s.lifecycle.presence_state == bhp::panel_mode_protocol::PresenceState::GracePeriod) {
  auto *dim = box(screen, 0, 0, width, height, 0);
  lv_obj_set_style_bg_opa(dim, LV_OPA_50, 0);
 }
}
const std::vector<LvglPanelDisplay::SurfaceRenderer> &LvglPanelDisplay::surface_renderers() {
 // @extension-point panel_surface_renderers -- one entry per app descriptor surface.
 static const std::vector<SurfaceRenderer> renderers{
  {"home", &LvglPanelDisplay::draw_home},
  {"study", &LvglPanelDisplay::draw_study},
  {"agenda", &LvglPanelDisplay::draw_agenda},
 };
 return renderers;
}
const LvglPanelDisplay::SurfaceRenderer *LvglPanelDisplay::find_surface_renderer(const std::string &surface_id) {
 for(const auto &renderer : surface_renderers()) if(surface_id == renderer.surface_id) return &renderer;
 return nullptr;
}
void LvglPanelDisplay::draw_home(lv_obj_t *, lv_obj_t *content, const PanelScene &s) {
 label(content, "Home", 24, 16, 896, 52, &bhp_font_40, 0xf4f0e8);
 const auto page=application_grid_page(s.applications,s.home_page);
 if(page.tiles.empty()) label(content,"No apps enabled",24,178,896,37,&bhp_font_28,0xaab8b7,LV_TEXT_ALIGN_CENTER);
 for(const auto &placement:page.tiles){
  const auto &app=s.applications.applications.at(placement.application_index);
  auto *tile=box(content,placement.x,placement.y,placement.width,placement.height,0x1c2d31,12);
  launcher_icon(tile,app,placement);
  label(tile,app.title,0,placement.title_y,placement.width,37,&bhp_font_28,0xf4f0e8,LV_TEXT_ALIGN_CENTER);
  label(tile,app.subtitle,0,placement.subtitle_y,placement.width,31,&bhp_font_24,app.preview?0xe9c480:0xaab8b7,LV_TEXT_ALIGN_CENTER);
  hit_targets_.push_back({layout::content_x+placement.x,layout::content_y+placement.y,placement.width,placement.height,app.launch_intent});
 }
 if(page.page_count>1){
  const auto page_button=[&](int x,const std::string &title,const std::string &intent){
   auto *control=box(content,x,360,176,64,0x293f43,12);
   label(control,title,0,13,176,37,&bhp_font_28,0xdbe5df,LV_TEXT_ALIGN_CENTER);
   hit_targets_.push_back({layout::content_x+x,layout::content_y+360,176,64,intent});
  };
  if(s.home_page>0)page_button(24,"Previous","shell.apps_previous");
  if(s.home_page+1<page.page_count)page_button(744,"Next","shell.apps_next");
  label(content,std::to_string(s.home_page+1)+" / "+std::to_string(page.page_count),224,373,496,37,&bhp_font_28,0xaab8b7,LV_TEXT_ALIGN_CENTER);
 }
}
void LvglPanelDisplay::draw_study(lv_obj_t *screen, lv_obj_t *content, const PanelScene &s) {
 vocabulary_panel::gui::lvgl::render_vocabulary_surface(content,
  {s.study_state, s.card_word, s.card_definition, s.card_example, s.provisional, s.can_back, s.can_undo});
 const auto action_target = [this](int index, int target_width, const std::string &intent) {
  hit_targets_.push_back({layout::content_x + layout::action_x(index), layout::content_y + layout::actions_y,
                          target_width, layout::action_height, intent});
 };
 if(s.study_state == "front" || s.study_state == "back") {
  hit_targets_.push_back({layout::content_x + layout::inset, layout::content_y + 16,
                          layout::text_width, layout::actions_y - 24, "study.flip"});
  if(s.study_state == "back") {
   const std::string intents[]{"study.rate.again", "study.rate.hard", "study.rate.good", "study.rate.easy"};
   for(int i = 0; i < 4; ++i) action_target(i, layout::action_width, intents[i]);
  } else {
   action_target(0, layout::primary_width, "study.flip");
   action_target(3, layout::action_width, "study.next");
  }
 }
 if(s.study_state == "resume" || s.study_state == "complete")
  action_target(0, layout::primary_width, "study.next");
 const auto header_action = [&](const std::string &text, int x, int target_width, std::uint32_t color,
                                const std::string &intent) {
  auto *control = box(screen, x, 80, target_width, 56, 0x293f43, 10);
  label(control, text, 8, 9, target_width - 16, 37, &bhp_font_28, color, LV_TEXT_ALIGN_CENTER);
  hit_targets_.push_back({x, 80, target_width, 56, intent});
 };
 if(s.can_back) header_action("Back", 720, 120, 0xdbe5df, "study.back");
 if(s.can_undo) header_action("Undo", 856, 128, 0xe9c480, "study.rating.undo");
}
void LvglPanelDisplay::draw_agenda(lv_obj_t *, lv_obj_t *content, const PanelScene &s) {
 const auto &state = s.agenda_state;
 if(state == "today" || state == "stale") {
  label(content, "Birthdays", 24, 16, 240, 31, &bhp_font_24, 0x8db5a7);
  label(content, "Today and next 7 days", 304, 16, 592, 31, &bhp_font_24, 0xaab8b7);
  label(content, "Rowan", 24, 56, 256, 44, &bhp_font_36, 0xf4f0e8);
  label(content, "Today", 304, 60, 592, 37, &bhp_font_28, 0xf4f0e8);
  label(content, "Alex", 24, 108, 256, 44, &bhp_font_36, 0xf4f0e8);
  label(content, "Sunday, Sep 27", 304, 112, 592, 37, &bhp_font_28, 0xf4f0e8);
  label(content, "Your day", 24, 184, 896, 31, &bhp_font_24, 0x8db5a7);
  const std::string times[]{"All day", "09:30", "14:00"};
  const std::string events[]{"A day for making", "Garden planning", "Busy"};
  for(int i = 0; i < 3; ++i) {
   label(content, times[i], 24, 232 + i * 52, 136, 37, &bhp_font_28, 0xaab8b7);
   label(content, events[i], 184, 228 + i * 52, 712, 44, &bhp_font_36, 0xf4f0e8);
  }
 } else {
  std::string title, detail, note;
  if(state == "expired") { title = "Agenda needs updating"; detail = "Current details are unavailable."; }
  else if(state == "composer") {
   title = "Recording preview"; detail = "Recording is not available in this preview.";
   note = "Microphone off. Nothing will be sent.";
  } else if(state == "recording") {
   title = "Recording preview"; detail = "Microphone off. Nothing is being recorded.";
   note = "Nothing will be sent.";
  } else if(state == "text_preview") {
   title = "Draft preview"; detail = "Happy birthday, Rowan. Wishing you a wonderful day.";
   note = "Sample text. Nothing will be sent.";
  } else if(state == "confirmation") {
   title = "Draft preview"; detail = "Sending is not available in this preview.";
   note = "Nothing will be sent.";
  } else if(state == "delivery_unknown") {
   title = "Delivery not confirmed"; detail = "Check before trying again.";
   note = "Sample state. Nothing was sent.";
  } else {
   title = "Companion unavailable"; detail = "Recording is unavailable.";
   note = "Microphone off.";
  }
  label(content, title, 24, 44, 896, 104, &bhp_font_40, 0xf4f0e8);
  if(state == "recording") {
   label(content, "00:12 / 01:00", 24, 156, 896, 62, &bhp_font_48, 0xf4f0e8);
   label(content, detail, 24, 236, 896, 111, &bhp_font_28, 0xaab8b7);
  } else label(content, detail, 24, 168, 896, state == "text_preview" ? 176 : 148,
               state == "text_preview" ? &bhp_font_36 : &bhp_font_28, 0xaab8b7);
  if(!note.empty()) label(content, note, 24, 376, 896, 31, &bhp_font_24, 0xe9c480);
 }
}
}
