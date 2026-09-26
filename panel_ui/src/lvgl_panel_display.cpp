#include "panel_ui/lvgl_panel_display.hpp"
#include "vocabulary_panel/gui/lvgl/lvgl_flash_card_renderer.h"
#include "vocabulary_panel/gui/lvgl/vocabulary_surface_layout.h"
#include <panel_fonts/panel_fonts.h>
#include <src/display/lv_display_private.h>
#include <png.h>
#include <algorithm>
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
  throw std::runtime_error("PANEL_UI.TEXT_LAYOUT_OVERFLOW");
}
lv_obj_t *box(lv_obj_t *parent,int x,int y,int w,int h,std::uint32_t color,int radius=0) {
 auto *o=lv_obj_create(parent);lv_obj_remove_style_all(o);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);
 lv_obj_set_style_bg_color(o,lv_color_hex(color),0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);
 lv_obj_set_style_radius(o,radius,0);lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE);return o;
}
bool clock_available(const PanelScene &s) {
 // The only supplied civil-time string is fixture data. Real clock provisioning
 // remains separate; unknown lifecycle time must never be presented as current.
 return s.fixture_data && !s.fixture_clock_untrusted &&
        s.lifecycle.display_mode != DisplayMode::Diagnostic &&
        s.lifecycle.solar_state != SolarState::Unknown &&
        (s.lifecycle.display_mode != DisplayMode::NightRed || s.lifecycle.solar_state == SolarState::Night) &&
        s.lifecycle.system_state == SystemState::Active;
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
 if(s.lifecycle.display_mode == DisplayMode::NightRed)
  return clock_available(s) ? std::vector<std::string>{s.clock_text} : std::vector<std::string>{};
 if(s.lifecycle.display_mode == DisplayMode::Diagnostic || s.fixture_clock_untrusted) return {};
 std::vector<std::string> result{status_text(s), acknowledgement_text(s.acknowledgement)};
 if(clock_available(s)) result.push_back(s.clock_text);
 if(s.surface_id == "study" && (s.study_state == "front" || s.study_state == "back")) {
  result.push_back(s.card_word);
  if(s.study_state == "back") { result.push_back(s.card_definition); result.push_back(s.card_example); }
 }
 return result;
}
}
LvglPanelDisplay::LvglPanelDisplay(bool sdl_window):pixels_(width*height),draw_buffer_(width*height) {
 if(lv_is_initialized()) throw std::runtime_error("PANEL_UI.LVGL_ALREADY_INITIALIZED");
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
  if(!bhp::panel_fonts::validate_glyph_coverage(text).supported)
   throw std::runtime_error("PANEL_UI.FONT_GLYPH_UNSUPPORTED");
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
  if(mode == DisplayMode::NightRed && !s.presentation_failure && clock_available(s))
   label(screen, s.clock_text, 40, (height - lv_font_get_line_height(&bhp_font_48)) / 2,
         944, 62, &bhp_font_48, s.night_red_rgb888, LV_TEXT_ALIGN_CENTER);
  return;
 }
 lv_obj_set_style_bg_color(screen, lv_color_hex(0x101c20), 0);
 label(screen, s.surface_id == "agenda" ? "Today" : "Vocabulary", 40, 16, 264, 52, &bhp_font_40, 0xdbe5df);
 if(clock_available(s))
  label(screen, s.clock_text, 752, 10, 232, 62, &bhp_font_48, 0xf4f0e8, LV_TEXT_ALIGN_RIGHT);
 if(s.fixture_data) label(screen, "Preview · sample data", 320, 28, 400, 31, &bhp_font_24, 0x8db5a7);
 auto *content = box(screen, layout::content_x, layout::content_y, layout::content_width,
                     layout::content_height, 0x1c2d31, 18);
 if(mode == DisplayMode::Diagnostic || s.fixture_clock_untrusted) {
  label(content, "Time unavailable", 24, 60, 896, 104, &bhp_font_40, 0xf4f0e8);
  label(content, "Waiting for the clock to be ready.", 24, 184, 896, 111, &bhp_font_28, 0xaab8b7);
  return;
 }
 const auto status = status_text(s);
 if(!status.empty()) label(screen, status, 40, 72, 650, 31, &bhp_font_24, 0xe9c480);
 const auto acknowledgement = acknowledgement_text(s.acknowledgement);
 if(!acknowledgement.empty()) label(screen, acknowledgement, 40, 107, 650, 31, &bhp_font_24, 0xe9c480);
 if(s.surface_id == "study") {
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
 } else {
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
 if(s.lifecycle.presence_state == bhp::panel_mode_protocol::PresenceState::GracePeriod) {
  auto *dim = box(screen, 0, 0, width, height, 0);
  lv_obj_set_style_bg_opa(dim, LV_OPA_50, 0);
 }
}
}
