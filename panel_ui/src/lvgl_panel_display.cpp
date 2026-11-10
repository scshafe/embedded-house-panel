#include "panel_ui/lvgl_panel_display.hpp"
#include "panel_ui/panel_app_descriptor.hpp"
#include "vocabulary_panel/gui/lvgl/lvgl_flash_card_renderer.h"
#include "vocabulary_panel/gui/lvgl/vocabulary_surface_layout.h"
#include <panel_fonts/panel_fonts.h>
#include <src/display/lv_display_private.h>
#include <png.h>
#include <algorithm>
#include <cstdio>
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
// Labels cut to their box in the frame being drawn (reset by draw_scene; LVGL is single-threaded).
int cut_labels = 0;
void label(lv_obj_t *parent, const std::string &text, int x, int y, int w, int maximum_height,
           const lv_font_t *font, std::uint32_t color, lv_text_align_t alignment = LV_TEXT_ALIGN_LEFT) {
 auto *o = lv_label_create(parent);
 lv_label_set_long_mode(o, LV_LABEL_LONG_MODE_WRAP);
 lv_obj_set_pos(o, x, y);
 lv_obj_set_width(o, w);
 lv_obj_set_style_text_font(o, font, 0);
 lv_obj_set_style_text_color(o, lv_color_hex(color), 0);
 lv_obj_set_style_text_align(o, alignment, 0);
 lv_obj_set_style_text_line_space(o, 0, 0);
 if(vocabulary_panel::gui::lvgl::set_label_text_within(o, text, maximum_height)) ++cut_labels;
}
// Every field that carries text from outside the panel: Lexinet, Spotify, the
// reporter, voice-lab and the owner's typing. Each goes through the one fit rule.
std::vector<std::pair<const char *, std::string *>> live_fields(PanelScene &s) {
 std::vector<std::pair<const char *, std::string *>> fields{
  {"card_word", &s.card_word}, {"card_definition", &s.card_definition}, {"card_example", &s.card_example},
  {"music.speaker", &s.music.speaker}, {"music.title", &s.music.title}, {"music.artists", &s.music.artists},
  {"music.notice", &s.music.notice}, {"report.text", &s.report.text}, {"report.notice", &s.report.notice},
  {"voice_lab.prompt_text", &s.voice_lab.prompt_text}, {"voice_lab.verification_uri", &s.voice_lab.verification_uri},
  {"voice_lab.user_code", &s.voice_lab.user_code}, {"voice_lab.approval_endpoint", &s.voice_lab.approval_endpoint},
  {"voice_lab.approval_node", &s.voice_lab.approval_node}, {"voice_lab.approval_expires", &s.voice_lab.approval_expires},
  {"voice_lab.approval_digest", &s.voice_lab.approval_digest}, {"voice_lab.notice", &s.voice_lab.notice}};
 for(auto &line : s.report.lines) fields.emplace_back("report.lines", &line);
 return fields;
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
std::uint32_t temperature_color(double celsius) {
 // Both forecast roles use the same actual-temperature scale. A day's high can
 // be cold, and its low can be hot; the labels never determine their colors.
 if(celsius <= 0) return 0x7faee8;
 if(celsius <= 10) return 0x79c5d6;
 if(celsius <= 20) return 0x9bc996;
 if(celsius <= 30) return 0xe9c480;
 return 0xec8b78;
}
std::string temperature_text(double celsius, const std::string &unit) {
 // The weather_panel codec's rules: round once in the chosen unit, never "-0"; Kelvin has no degree sign.
 const double value = unit == "K" ? celsius + 273.15 : unit == "C" ? celsius : celsius * 1.8 + 32;
 const long rounded = std::lround(value);
 return std::to_string(rounded == 0 ? 0L : rounded) + (unit == "K" ? " K" : "°" + unit);
}
/** ↑ or ↓, drawn: the panel's font subset has no arrows. 14 px wide, 22 px tall, at (x, y). */
void sun_arrow(lv_obj_t *parent, int x, int y, bool up, std::uint32_t color) {
 static const lv_point_precise_t up_points[]{{0, 8}, {7, 0}, {14, 8}};
 static const lv_point_precise_t down_points[]{{0, 14}, {7, 22}, {14, 14}};
 auto *head = lv_line_create(parent);
 lv_line_set_points(head, up ? up_points : down_points, 3);
 lv_obj_set_pos(head, x, y);
 lv_obj_set_style_line_color(head, lv_color_hex(color), 0);
 lv_obj_set_style_line_width(head, 3, 0);
 box(parent, x + 6, y + 1, 3, 21, color);
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
 if(s.surface_id == "report") {
  result.push_back(s.report.text);
  result.push_back(s.report.notice);
  for(const auto &line : s.report.lines) result.push_back(line);
 }
 // Live Spotify text is validated like the card's: unsupported glyphs fail before drawing.
 if(s.surface_id == "music") {
  result.push_back(s.music.speaker);
  result.push_back(s.music.notice);
  if(s.music.state == "playing" || s.music.state == "paused") { result.push_back(s.music.title); result.push_back(s.music.artists); }
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
std::uint64_t LvglPanelDisplay::render(const PanelScene &scene) {
 // Live text never ends the UI: it is fitted here, once, for every surface.
 PanelScene s = scene;
 text_fit_ = {};
 for(auto &[name, text] : live_fields(s)) {
  auto fitted = bhp::panel_fonts::fit_to_subset(*text, live_field_code_points);
  if(fitted.replaced || fitted.truncated) {
   *text = std::move(fitted.text);
   if(std::find(text_fit_.fitted_fields.begin(), text_fit_.fitted_fields.end(), name) == text_fit_.fitted_fields.end())
    text_fit_.fitted_fields.emplace_back(name);
  }
 }
 auto validated=text_renderer_->render({s.lifecycle.display_mode,ShellTransitionState::Stable,s.surface_id,scene_text(s)});
 if(!validated) throw std::runtime_error(std::string(validated.error().code_string()));
 for(const auto &text : validated.value().visible_text_runs)
  if(!bhp::panel_fonts::validate_glyph_coverage(text, s.lifecycle.display_mode == DisplayMode::NightRed ?
       bhp::panel_fonts::FontSubset::Clock : bhp::panel_fonts::FontSubset::Latin).supported)
   throw std::runtime_error("PANEL_UI.FONT_GLYPH_UNSUPPORTED");
 if(clock_available(s) && (!clock_text_valid(s.clock_text) || s.clock_transition_milli > 1000 ||
    (s.clock_transition_milli < 1000 && !clock_text_valid(s.clock_previous_text))))
  throw std::runtime_error("PANEL_UI.CLOCK_FRAME_INVALID");
 cut_labels = 0;
 draw_scene(s);lv_obj_invalidate(lv_display_get_screen_active(display_));lv_refr_now(display_);
 text_fit_.cut_labels = cut_labels;
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
  // Report, on every daytime surface (bug-reports-and-self-repair.md). The day bar
  // looks the same everywhere; on the report screen itself it does nothing.
  auto *report = box(screen, 592, 12, 112, 52, 0x293f43, 12);
  label(report, "Report", 0, 11, 112, 31, &bhp_font_24, 0xdbe5df, LV_TEXT_ALIGN_CENTER);
  if(s.report.update_ready) box(report, 96, 6, 10, 10, 0xe9c480, 5);
  if(s.surface_id != "report") hit_targets_.push_back({592, 12, 112, 52, "shell.open_report"});
  const bool preview = s.fixture_data || s.clock_source == "fixture" || s.weather_fixture;
  if(preview) label(screen, "Preview", 208, s.weather_stale ? 4 : 22, 192, 31, &bhp_font_24, 0x8db5a7);
  if(s.weather_stale) label(screen, "Weather stale", 208, preview ? 37 : 22, 192, 31, &bhp_font_24, 0xe9c480);
  if(clock_available(s)) draw_clock(screen, s, &bhp_font_48, 32, 33, 0xf4f0e8);
  else label(screen, "—", 360, 6, 304, 62, &bhp_font_48, 0xf4f0e8, LV_TEXT_ALIGN_CENTER);
  const bool sun_times = s.weather_bar == "sun_times";
  for(int index = 0; index < 2; ++index) {
   const int x = index == 0 ? 712 : 856;
   if(sun_times) {
    // Sun times in neutral text, the place's local time (ADR 0015): "↑ 6:40" and "↓ 16:48".
    label(screen, index == 0 ? "Sunrise" : "Sunset", x, 2, 128, 31, &bhp_font_24, 0xaab8b7, LV_TEXT_ALIGN_RIGHT);
    const std::string &time = index == 0 ? s.weather_sunrise : s.weather_sunset;
    const std::string text = s.weather_available ? (time.size() == 5 && time[0] == '0' ? time.substr(1) : time) : "—";
    lv_point_t size{};
    lv_text_get_size(&size, text.c_str(), &bhp_font_28, 0, 0, 128, LV_TEXT_FLAG_NONE);
    label(screen, text, x, 33, 128, 37, &bhp_font_28, s.weather_available ? 0xf4f0e8 : 0xaab8b7, LV_TEXT_ALIGN_RIGHT);
    if(s.weather_available) sun_arrow(screen, x + 128 - size.x - 22, 40, index == 0, 0xf4f0e8);
    continue;
   }
   const double temperature = index == 0 ? s.weather_high_celsius : s.weather_low_celsius;
   label(screen, index == 0 ? "High" : "Low", x, 2, 128, 31, &bhp_font_24, 0xaab8b7, LV_TEXT_ALIGN_RIGHT);
   label(screen, s.weather_available ? temperature_text(temperature, s.weather_temperature_unit) : "—",
         x, 33, 128, 37, &bhp_font_28, s.weather_available ? temperature_color(temperature) : 0xaab8b7,
         LV_TEXT_ALIGN_RIGHT);
  }
  // A tap on the forecast opens the weather menu (ADR 0015); on the menu itself it does nothing.
  if(s.surface_id != "weather") hit_targets_.push_back({712, 0, 272, 72, "shell.open_weather"});
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
  {"voice_lab", &LvglPanelDisplay::draw_voice_lab},
  {"music", &LvglPanelDisplay::draw_music},
  {"report", &LvglPanelDisplay::draw_report},
  {"weather", &LvglPanelDisplay::draw_weather},
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
 cut_labels += vocabulary_panel::gui::lvgl::render_vocabulary_surface(content,
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
static std::string minutes_seconds(std::uint64_t milliseconds) {
 const auto seconds = milliseconds / 1000;
 char text[16];
 std::snprintf(text, sizeof text, "%02llu:%02llu", static_cast<unsigned long long>(seconds / 60),
               static_cast<unsigned long long>(seconds % 60));
 return text;
}
void LvglPanelDisplay::draw_voice_lab(lv_obj_t *, lv_obj_t *content, const PanelScene &s) {
 const auto &v = s.voice_lab;
 const auto &state = v.state;
 label(content, "Voice lab", 24, 16, 240, 31, &bhp_font_24, 0x8db5a7);
 if(state == "ready" || state == "recording" || state == "review")
  label(content, "Prompt " + std::to_string(v.prompt_number) + " of " + std::to_string(v.prompt_total) + " · " +
        std::to_string(v.missing) + " missing", 304, 16, 616, 31, &bhp_font_24, 0xaab8b7, LV_TEXT_ALIGN_RIGHT);
 const auto action = [this, content](int index, int target_width, const std::string &title, const std::string &intent,
                                     std::uint32_t color) {
  auto *control = box(content, layout::action_x(index), layout::actions_y, target_width, layout::action_height, 0x293f43, 12);
  label(control, title, 0, 13, target_width, 37, &bhp_font_28, color, LV_TEXT_ALIGN_CENTER);
  hit_targets_.push_back({layout::content_x + layout::action_x(index), layout::content_y + layout::actions_y,
                          target_width, layout::action_height, intent});
 };
 const auto queued = [&](int y) {
  if(v.queued_takes == 0) return;
  label(content, std::to_string(v.queued_takes) + (v.queued_takes == 1 ? " take" : " takes") + " waiting to upload",
        24, y, 896, 31, &bhp_font_24, 0xaab8b7);
 };
 if(state == "signed_out") {
  label(content, "Sign in to voice-lab", 24, 56, 896, 52, &bhp_font_40, 0xf4f0e8);
  label(content, "The panel records takes for your voice-lab corpus.", 24, 128, 896, 37, &bhp_font_28, 0xaab8b7);
  label(content, "Sign in once, from your phone.", 24, 168, 896, 37, &bhp_font_28, 0xaab8b7);
  queued(240);
  action(0, layout::primary_width, "Sign in", "voice_lab.sign_in", 0xf4f0e8);
  return;
 }
 if(state == "approve") {
  label(content, "Approve your PC", 24, 56, 896, 52, &bhp_font_40, 0xf4f0e8);
  label(content, "Takes upload only to", 24, 120, 896, 31, &bhp_font_24, 0xaab8b7);
  label(content, v.approval_endpoint, 24, 152, 896, 37, &bhp_font_28, 0xf4f0e8);
  label(content, "Node " + v.approval_node + " · tag:voice-lab · until " + v.approval_expires, 24, 196, 896, 31,
        &bhp_font_24, 0xaab8b7);
  label(content, "Record digest", 24, 240, 896, 31, &bhp_font_24, 0xaab8b7);
  label(content, v.approval_digest.substr(0, 32), 24, 272, 896, 31, &bhp_font_24, 0xe9c480);
  label(content, v.approval_digest.size() > 32 ? v.approval_digest.substr(32) : std::string(), 24, 304, 896, 31,
        &bhp_font_24, 0xe9c480);
  action(0, layout::primary_width, "Approve", "voice_lab.approve", 0xf4f0e8);
  return;
 }
 if(state == "signing_in") {
  label(content, "Sign in to voice-lab", 24, 56, 896, 52, &bhp_font_40, 0xf4f0e8);
  label(content, "On your phone, open", 24, 128, 896, 37, &bhp_font_28, 0xaab8b7);
  label(content, v.verification_uri, 24, 168, 896, 44, &bhp_font_36, 0xf4f0e8);
  label(content, "and enter the code", 24, 224, 896, 37, &bhp_font_28, 0xaab8b7);
  label(content, v.user_code, 24, 264, 896, 62, &bhp_font_48, 0xe9c480);
  label(content, "The code expires in 15 minutes.", 24, 376, 896, 31, &bhp_font_24, 0xaab8b7);
  return;
 }
 if(state == "ready") {
  label(content, v.prompt_text, 24, 64, 896, 176, &bhp_font_36, 0xf4f0e8);
  queued(296);
  if(!v.notice.empty()) label(content, v.notice, 24, 336, 896, 31, &bhp_font_24, 0xe9c480);
  action(0, layout::primary_width, "Record", "voice_lab.record", 0xf4f0e8);
  action(3, layout::action_width, "Skip", "voice_lab.skip", 0xdbe5df);
  return;
 }
 if(state == "recording") {
  label(content, v.prompt_text, 24, 56, 896, 74, &bhp_font_28, 0xaab8b7);
  label(content, minutes_seconds(v.elapsed_ms) + " / " + minutes_seconds(v.maximum_ms), 24, 140, 896, 62,
        &bhp_font_48, 0xf4f0e8);
  for(std::size_t channel = 0; channel < v.level_tenths.size() && channel < 6; ++channel) {
   const int x = 24 + static_cast<int>(channel) * 150;
   box(content, x, 224, 136, 40, 0x293f43, 8);
   const int level = std::clamp(v.level_tenths[channel], -600, 0);
   const int fill = (level + 600) * 136 / 600;
   if(fill > 0) box(content, x, 224, fill, 40, level > -30 ? 0xe9c480 : 0x8db5a7, 8);
   label(content, std::to_string(channel + 1), x, 270, 136, 31, &bhp_font_24, 0xaab8b7, LV_TEXT_ALIGN_CENTER);
  }
  action(0, layout::primary_width, "Stop", "voice_lab.stop", 0xf4f0e8);
  return;
 }
 if(state == "review") {
  label(content, "Take " + minutes_seconds(v.take_ms), 24, 56, 896, 52, &bhp_font_40, 0xf4f0e8);
  label(content, v.prompt_text, 24, 120, 896, 74, &bhp_font_28, 0xaab8b7);
  char peak[48];
  std::snprintf(peak, sizeof peak, "Loudest peak %.1f dB", v.take_peak_tenths / 10.0);
  label(content, peak, 24, 208, 896, 37, &bhp_font_28, v.take_peak_tenths > -10 ? 0xe9c480 : 0xaab8b7);
  queued(256);
  action(0, layout::primary_width, "Keep", "voice_lab.keep", 0xf4f0e8);
  action(2, layout::action_width, "Retake", "voice_lab.retake", 0xdbe5df);
  action(3, layout::action_width, "Reject", "voice_lab.reject", 0xe9c480);
  return;
 }
 if(state == "complete") {
  label(content, "Every prompt is recorded", 24, 56, 896, 52, &bhp_font_40, 0xf4f0e8);
  label(content, "New prompts appear when voice-lab adds them.", 24, 128, 896, 37, &bhp_font_28, 0xaab8b7);
  queued(184);
  if(!v.notice.empty()) label(content, v.notice, 24, 224, 896, 31, &bhp_font_24, 0xe9c480);
  return;
 }
 label(content, "Recorder unavailable", 24, 56, 896, 52, &bhp_font_40, 0xf4f0e8);
 label(content, "The microphone is off or the recorder is not running.", 24, 128, 896, 74, &bhp_font_28, 0xaab8b7);
 label(content, "Nothing is being recorded.", 24, 376, 896, 31, &bhp_font_24, 0xe9c480);
}
void LvglPanelDisplay::draw_music(lv_obj_t *, lv_obj_t *content, const PanelScene &s) {
 const auto &m = s.music;
 const auto &state = m.state;
 label(content, "Music", 24, 16, 240, 31, &bhp_font_24, 0x8db5a7);
 const auto action = [this, content](int index, int target_width, const std::string &title, const std::string &intent,
                                     std::uint32_t color) {
  auto *control = box(content, layout::action_x(index), layout::actions_y, target_width, layout::action_height, 0x293f43, 12);
  label(control, title, 0, 13, target_width, 37, &bhp_font_28, color, LV_TEXT_ALIGN_CENTER);
  hit_targets_.push_back({layout::content_x + layout::action_x(index), layout::content_y + layout::actions_y,
                          target_width, layout::action_height, intent});
 };
 const auto notice = [&] {
  if(!m.notice.empty()) label(content, m.notice, 24, 312, 896, 31, &bhp_font_24, 0xe9c480);
 };
 const auto message = [&](const std::string &title, const std::string &detail) {
  label(content, title, 24, 56, 896, 104, &bhp_font_40, 0xf4f0e8);
  label(content, detail, 24, 168, 896, 74, &bhp_font_28, 0xaab8b7);
 };
 if(state == "playing" || state == "paused") {
  label(content, m.title, 24, 64, 896, 88, &bhp_font_36, 0xf4f0e8);
  label(content, m.artists, 24, 160, 896, 74, &bhp_font_28, 0xaab8b7);
  const auto volume = m.supports_volume && m.volume_percent >= 0 ? " · Volume " + std::to_string(m.volume_percent) + "%"
                                                                  : std::string();
  label(content, (state == "playing" ? "Playing on " : "Paused on ") + m.speaker + volume, 24, 248, 896, 62,
        &bhp_font_24, 0x8db5a7);
  notice();
  if(state == "playing") action(0, layout::action_width, "Pause", "music.pause", 0xf4f0e8);
  else action(0, layout::action_width, "Play", "music.play", 0xf4f0e8);
  action(1, layout::action_width, "Skip", "music.next", 0xdbe5df);
  if(m.supports_volume && m.volume_percent >= 0) {
   action(2, layout::action_width, "Volume -", "music.volume_down", 0xdbe5df);
   action(3, layout::action_width, "Volume +", "music.volume_up", 0xdbe5df);
  }
  return;
 }
 if(state == "ready") {
  message(m.speaker, "Music plays on the speaker through Spotify. Nothing plays on the panel.");
  if(m.supports_volume && m.volume_percent >= 0)
   label(content, "Volume " + std::to_string(m.volume_percent) + "%", 24, 260, 896, 31, &bhp_font_24, 0x8db5a7);
  notice();
  action(0, layout::primary_width, "Play here", "music.use_speaker", 0xf4f0e8);
  return;
 }
 if(state == "missing") {
  message(m.speaker + " is not available", "It may be asleep, off or on battery. Press a button on the speaker, then try again.");
  notice();
  action(0, layout::primary_width, "Try again", "music.use_speaker", 0xf4f0e8);
  return;
 }
 if(state == "restricted") {
  message(m.speaker + " cannot be controlled", "Spotify does not accept commands for this speaker right now.");
  notice();
  return;
 }
 if(state == "checking") {
  message("Looking for " + m.speaker, "Asking Spotify which speakers are available.");
  return;
 }
 if(state == "signed_out") {
  message("Sign in to Spotify", "The panel needs a one-time Spotify sign-in, done from your PC.");
  notice();
  return;
 }
 message("Music unavailable", "The music service is not running.");
 label(content, "Nothing plays on the panel.", 24, 376, 896, 31, &bhp_font_24, 0xe9c480);
}
void LvglPanelDisplay::draw_report(lv_obj_t *, lv_obj_t *content, const PanelScene &s) {
 const auto &r = s.report;
 label(content, "Report a problem", 24, 16, 400, 31, &bhp_font_24, 0x8db5a7);
 const auto key = [this, content](int x, int y, int w, const std::string &title, const std::string &intent,
                                  std::uint32_t color, int h = 50) {
  auto *control = box(content, x, y, w, h, 0x293f43, 10);
  label(control, title, 0, (h - 31) / 2, w, 31, &bhp_font_24, color, LV_TEXT_ALIGN_CENTER);
  hit_targets_.push_back({layout::content_x + x, layout::content_y + y, w, h, intent});
 };
 const auto action = [this, content](int index, int target_width, const std::string &title, const std::string &intent) {
  auto *control = box(content, layout::action_x(index), layout::actions_y, target_width, layout::action_height, 0x293f43, 12);
  label(control, title, 0, 13, target_width, 37, &bhp_font_28, 0xf4f0e8, LV_TEXT_ALIGN_CENTER);
  hit_targets_.push_back({layout::content_x + layout::action_x(index), layout::content_y + layout::actions_y,
                          target_width, layout::action_height, intent});
 };
 if(r.state == "compose") {
  if(!r.notice.empty()) label(content, r.notice, 424, 16, 496, 31, &bhp_font_24, 0xe9c480, LV_TEXT_ALIGN_RIGHT);
  else if(r.has_recording) label(content, "Spoken description recorded", 424, 16, 496, 31, &bhp_font_24, 0x8db5a7, LV_TEXT_ALIGN_RIGHT);
  auto *field = box(content, 24, 52, 896, 86, 0x293f43, 8);
  if(r.text.empty())
   label(field, "Type what went wrong, or just Send: the screen and logs go with it.", 12, 6, 872, 74, &bhp_font_28, 0xaab8b7);
  else
   label(field, (r.text.size() > 70 ? "..." + r.text.substr(r.text.size() - 70) : r.text) + "|", 12, 6, 872, 74,
         &bhp_font_28, 0xf4f0e8);
  // Lowercase keys, one closed intent each: the shell routes intents, not raw input.
  static const char *rows[3][10] = {{"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"},
                                    {"q", "w", "e", "r", "t", "y", "u", "i", "o", "p"},
                                    {"a", "s", "d", "f", "g", "h", "j", "k", "l", "'"}};
  static const char *row_ids[3][10] = {{"digit_1", "digit_2", "digit_3", "digit_4", "digit_5", "digit_6", "digit_7", "digit_8", "digit_9", "digit_0"},
                                       {"q", "w", "e", "r", "t", "y", "u", "i", "o", "p"},
                                       {"a", "s", "d", "f", "g", "h", "j", "k", "l", "apostrophe"}};
  for(int row = 0; row < 3; ++row)
   for(int column = 0; column < 10; ++column)
    key(24 + column * 90, 146 + row * 56, 82, rows[row][column], std::string("report.key.") + row_ids[row][column], 0xf4f0e8);
  static const char *last[10] = {"z", "x", "c", "v", "b", "n", "m", ",", ".", "?"};
  static const char *last_ids[10] = {"z", "x", "c", "v", "b", "n", "m", "comma", "period", "question"};
  for(int column = 0; column < 10; ++column)
   key(24 + column * 90, 314, 82, last[column], std::string("report.key.") + last_ids[column], 0xf4f0e8);
  key(24, 370, 124, "Reports", "report.show_list", 0xdbe5df, 56);
  key(156, 370, 136, "Record", "report.record", 0xf4f0e8, 56);
  key(300, 370, 220, "Space", "report.key.space", 0xdbe5df, 56);
  key(528, 370, 112, "Delete", "report.key.backspace", 0xdbe5df, 56);
  key(648, 370, 128, "Cancel", "report.cancel", 0xe9c480, 56);
  key(784, 370, 136, "Send", "report.send", 0xf4f0e8, 56);
  return;
 }
 if(r.state == "recording") {
  label(content, "Describe what went wrong", 24, 56, 896, 52, &bhp_font_40, 0xf4f0e8);
  label(content, minutes_seconds(r.elapsed_ms) + " / 01:00", 24, 128, 896, 62, &bhp_font_48, 0xf4f0e8);
  label(content, "Only while the microphone is on. Stop when you are done.", 24, 210, 896, 74, &bhp_font_28, 0xaab8b7);
  action(0, layout::primary_width, "Stop", "report.stop");
  return;
 }
 if(r.state == "sending") {
  label(content, "Saving your report", 24, 56, 896, 52, &bhp_font_40, 0xf4f0e8);
  label(content, "With the screen you were on and the last minutes of logs.", 24, 128, 896, 74, &bhp_font_28, 0xaab8b7);
  return;
 }
 if(r.state == "sent") {
  label(content, "Report saved", 24, 56, 896, 52, &bhp_font_40, 0xf4f0e8);
  label(content, "It goes to your PC whenever the PC is reachable. Follow it under Reports.", 24, 128, 896, 74,
        &bhp_font_28, 0xaab8b7);
  label(content, r.sent_id, 24, 220, 896, 31, &bhp_font_24, 0x8db5a7);
  action(0, layout::primary_width, "Done", "report.done");
  action(3, layout::action_width, "Reports", "report.show_list");
  return;
 }
 if(r.state == "list") {
  if(r.update_ready) label(content, "An update is ready", 424, 16, 496, 31, &bhp_font_24, 0xe9c480, LV_TEXT_ALIGN_RIGHT);
  if(r.lines.empty()) label(content, "No reports yet.", 24, 64, 896, 37, &bhp_font_28, 0xaab8b7);
  for(std::size_t at = 0; at < r.lines.size() && at < 6; ++at)
   label(content, r.lines[at], 24, 56 + static_cast<int>(at) * 44, 896, 37, &bhp_font_28, 0xf4f0e8);
  action(0, layout::primary_width, "Done", "report.done");
  return;
 }
 label(content, "Reporting unavailable", 24, 56, 896, 52, &bhp_font_40, 0xf4f0e8);
 label(content, "The reporter is not running. Nothing was saved or sent.", 24, 128, 896, 74, &bhp_font_28, 0xaab8b7);
 action(0, layout::primary_width, "Done", "report.done");
}
void LvglPanelDisplay::draw_weather(lv_obj_t *, lv_obj_t *content, const PanelScene &s) {
 // ADR 0015: the units, what the bar shows, and where the forecast is for. Display only: the
 // panel's day and night stay on the supervisor's own location.
 const auto &w = s.weather;
 const auto button = [this, content](int x, int y, int bw, int bh, const std::string &title, const std::string &intent,
                                     bool selected = false, std::uint32_t color = 0xf4f0e8) {
  const std::uint32_t fill = selected ? 0x2f6b5a : 0x293f43;
  auto *control = box(content, x, y, bw, bh, fill, 12);
  // Every button has the border, so a selected one's title sits where an unselected one's does.
  lv_obj_set_style_border_color(control, lv_color_hex(selected ? 0x8db5a7 : fill), 0);
  lv_obj_set_style_border_width(control, 3, 0);
  label(control, title, 0, (bh - 37) / 2, bw, 37, &bhp_font_28, color, LV_TEXT_ALIGN_CENTER);
  hit_targets_.push_back({layout::content_x + x, layout::content_y + y, bw, bh, intent});
 };
 const auto heading = [content](const std::string &text) {
  label(content, text, 24, 12, 600, 31, &bhp_font_24, 0x8db5a7);
 };
 const auto search_footer = [&](int y) {
  button(24, y, 300, 64, "Search again", "weather.location.choose");
  button(340, y, 200, 64, "Back", "weather.search.back");
 };
 if(w.state == "menu" || w.state == "automatic_unavailable") {
  heading("Weather");
  const int row_x = 220;
  label(content, "Units", 24, 56 + 13, 180, 37, &bhp_font_28, 0xaab8b7);
  button(row_x, 56, 150, 64, "°F", "weather.unit.fahrenheit", s.weather_temperature_unit == "F");
  button(row_x + 166, 56, 150, 64, "°C", "weather.unit.celsius", s.weather_temperature_unit == "C");
  button(row_x + 332, 56, 150, 64, "K", "weather.unit.kelvin", s.weather_temperature_unit == "K");
  label(content, "Show", 24, 136 + 13, 180, 37, &bhp_font_28, 0xaab8b7);
  button(row_x, 136, 280, 64, "High & low", "weather.bar.temperatures", s.weather_bar != "sun_times");
  button(row_x + 296, 136, 330, 64, "Sunrise & sunset", "weather.bar.sun_times", s.weather_bar == "sun_times");
  label(content, "Location", 24, 216 + 13, 180, 37, &bhp_font_28, 0xaab8b7);
  button(row_x, 216, 280, 64, "Automatic", "weather.location.automatic", w.automatic);
  button(row_x + 296, 216, 330, 64, "Choose a place…", "weather.location.choose", !w.automatic);
  if(w.state == "automatic_unavailable")
   label(content, "Automatic location unavailable. Choose a place.", 24, 292, 896, 37, &bhp_font_28, 0xe9c480);
  else
   label(content, (w.automatic ? "Approximate: " : "") + w.location_label, 24, 292, 896, 37, &bhp_font_28, 0xf4f0e8);
  auto *done = box(content, layout::action_x(0), layout::actions_y, layout::primary_width, layout::action_height, 0x293f43, 12);
  label(done, "Done", 0, 13, layout::primary_width, 37, &bhp_font_28, 0xf4f0e8, LV_TEXT_ALIGN_CENTER);
  hit_targets_.push_back({layout::content_x + layout::action_x(0), layout::content_y + layout::actions_y,
                          layout::primary_width, layout::action_height, "weather.done"});
  // CC BY 4.0 (ADR 0015, decision 1).
  label(content, "Weather data by Open-Meteo.com", 400, layout::actions_y + 17, 520, 31, &bhp_font_24, 0xaab8b7,
        LV_TEXT_ALIGN_RIGHT);
  return;
 }
 if(w.state == "search") {
  heading("Choose a place");
  auto *field = box(content, 24, 48, 896, 56, 0x293f43, 8);
  if(w.query.empty()) label(field, "Type a town or city", 12, 9, 872, 37, &bhp_font_28, 0xaab8b7);
  else label(field, w.query + "|", 12, 9, 872, 37, &bhp_font_28, 0xf4f0e8);
  // Keys at least 64 px (ADR 0015, decision 4), one closed intent each: the shell routes intents, not raw input.
  static const char *rows[3] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};
  for(int row = 0; row < 3; ++row) {
   const std::string keys = rows[row];
   const int start = row == 0 ? 24 : 69;
   for(std::size_t column = 0; column < keys.size(); ++column)
    button(start + static_cast<int>(column) * 90, 112 + row * 70, 84, 64, std::string(1, keys[column]),
           std::string("weather.key.") + keys[column]);
  }
  button(69 + 7 * 90, 252, 84, 64, "-", "weather.key.hyphen");
  button(69 + 8 * 90, 252, 84, 64, "'", "weather.key.apostrophe");
  button(24, 336, 150, 64, "Back", "weather.search.back", false, 0xdbe5df);
  button(184, 336, 300, 64, "Space", "weather.key.space", false, 0xdbe5df);
  button(494, 336, 170, 64, "Delete", "weather.key.backspace", false, 0xdbe5df);
  button(674, 336, 246, 64, "Search", "weather.search.submit");
  return;
 }
 if(w.state == "searching") {
  heading("Choose a place");
  label(content, "Searching for “" + w.query + "”…", 24, 64, 896, 52, &bhp_font_40, 0xf4f0e8);
  button(24, 336, 200, 64, "Back", "weather.search.back");
  return;
 }
 if(w.state == "results") {
  heading("Choose a place");
  for(std::size_t at = 0; at < w.results.size() && at < 5; ++at)
   button(24, 48 + static_cast<int>(at) * 58, 896, 52, w.results[at], "weather.result." + std::to_string(at + 1));
  search_footer(344);
  return;
 }
 if(w.state == "no_results") {
  heading("Choose a place");
  label(content, "No place called “" + w.query + "”", 24, 64, 896, 52, &bhp_font_40, 0xf4f0e8);
  label(content, "Check the spelling, or try a nearby town.", 24, 136, 896, 37, &bhp_font_28, 0xaab8b7);
  search_footer(344);
  return;
 }
 heading("Choose a place");
 label(content, "Search unavailable", 24, 64, 896, 52, &bhp_font_40, 0xf4f0e8);
 label(content, "The place search could not be reached. Try again later, or use Automatic.", 24, 136, 896, 74,
       &bhp_font_28, 0xaab8b7);
 search_footer(344);
}
}
