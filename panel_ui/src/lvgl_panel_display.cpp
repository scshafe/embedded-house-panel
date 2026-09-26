#include "panel_ui/lvgl_panel_display.hpp"
#include "vocabulary_panel/gui/lvgl/lvgl_flash_card_renderer.h"
#include <src/display/lv_display_private.h>
#include <png.h>
#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <utility>

namespace house_panel::panel_ui {
namespace {
constexpr int width=1024,height=600;
void label(lv_obj_t *parent,const std::string &text,int x,int y,int w,const lv_font_t *font,std::uint32_t color) {
 auto *o=lv_label_create(parent); lv_label_set_text(o,text.c_str());
 lv_label_set_long_mode(o,LV_LABEL_LONG_MODE_WRAP); lv_obj_set_pos(o,x,y);lv_obj_set_width(o,w);
 lv_obj_set_style_text_font(o,font,0);lv_obj_set_style_text_color(o,lv_color_hex(color),0);
}
lv_obj_t *box(lv_obj_t *parent,int x,int y,int w,int h,std::uint32_t color,int radius=0) {
 auto *o=lv_obj_create(parent);lv_obj_remove_style_all(o);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);
 lv_obj_set_style_bg_color(o,lv_color_hex(color),0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);
 lv_obj_set_style_radius(o,radius,0);lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE);return o;
}
std::vector<std::string> scene_text(const PanelScene &s) {
 if(s.presentation_failure||s.lifecycle.display_mode==bhp::panel_mode_protocol::DisplayMode::NightRed||s.lifecycle.display_mode==bhp::panel_mode_protocol::DisplayMode::Suspended)return {};
 if(s.surface_id=="agenda"||(s.lifecycle.display_mode==bhp::panel_mode_protocol::DisplayMode::Diagnostic||s.fixture_clock_untrusted))return {s.clock_text,s.acknowledgement};
 return {s.clock_text,s.card_word,s.card_definition,s.card_example,s.delivery_status,s.acknowledgement};
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
 for(const auto &text:scene_text(s)) for(unsigned char c:text)
  if((c<32 && c!='\n') || c>126) throw std::runtime_error("PANEL_UI.BUILTIN_GLYPH_UNSUPPORTED");
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
 auto *screen=lv_display_get_screen_active(display_);lv_obj_clean(screen);lv_obj_remove_style_all(screen);
 lv_obj_set_style_bg_opa(screen,LV_OPA_COVER,0);lv_obj_remove_flag(screen,LV_OBJ_FLAG_SCROLLABLE);hit_targets_.clear();
 auto mode=s.lifecycle.display_mode;
 if(s.presentation_failure || mode==bhp::panel_mode_protocol::DisplayMode::Suspended) {
  lv_obj_set_style_bg_color(screen,lv_color_hex(0),0);return;
 }
 if(mode==bhp::panel_mode_protocol::DisplayMode::NightRed) {
  lv_obj_set_style_bg_color(screen,lv_color_hex(s.night_red_rgb888),0);return;
 }
 lv_obj_set_style_bg_color(screen,lv_color_hex(0x101c20),0);
 label(screen,"HOUSE / PANEL",40,26,300,&lv_font_montserrat_20,0xdbe5df);
 label(screen,s.clock_text,858,22,128,&lv_font_montserrat_32,0xf4f0e8);
 std::string status=s.offline ? "OFFLINE - CACHED SESSION" : s.stale ? "STALE DATA - LAST SYNC 2 HOURS AGO" : "FRIDAY, SEPTEMBER 25  /  FIXTURE DATA";
 if(mode==bhp::panel_mode_protocol::DisplayMode::Diagnostic||s.fixture_clock_untrusted) status="CLOCK UNTRUSTED - TIME-BASED ACTIONS UNAVAILABLE";
 if(s.lifecycle.presence_state==bhp::panel_mode_protocol::PresenceState::GracePeriod) status="PRESENCE GRACE - DIM FOR 120 SECONDS";
 label(screen,status,40,62,900,&lv_font_montserrat_14,0xb3bfb8);
 label(screen,s.surface_id=="agenda" ? "TODAY" : "VOCABULARY",40,103,600,&lv_font_montserrat_14,0x8db5a7);
 auto *content=box(screen,40,130,944,460,0x1c2d31,18);
 if(mode==bhp::panel_mode_protocol::DisplayMode::Diagnostic||s.fixture_clock_untrusted) {
  label(content,"Waiting for a trusted clock",32,85,850,&lv_font_montserrat_32,0xf4f0e8);
  label(content,s.fixture_clock_untrusted?"Fixture clock data is unavailable.\nNo dates or freshness claims are inferred.":"The supervisor has paused the daytime experience.\nNo dates or freshness claims are inferred.",32,155,850,&lv_font_montserrat_24,0xaab8b7);
  if(s.fixture_clock_untrusted)label(content,"Clock trust is a simulation fixture; guest clock remains synchronized.",32,340,850,&lv_font_montserrat_14,0xe9c480);
  return;
 }
 if(s.surface_id=="study") {
  vocabulary_panel::gui::lvgl::render_vocabulary_surface(content,{s.study_state,s.card_word,s.card_definition,s.card_example,s.provisional,s.can_back,s.can_undo});
  if(s.study_state=="front" || s.study_state=="back") {
   hit_targets_.push_back({72,158,850,328,"study.flip"});
   if(s.study_state=="back") for(int i=0;i<4;++i)hit_targets_.push_back({72+i*206,522,190,56,std::vector<std::string>{"study.rate.again","study.rate.hard","study.rate.good","study.rate.easy"}[i]});
   else {hit_targets_.push_back({72,522,220,56,"study.flip"});hit_targets_.push_back({690,522,190,56,"study.next"});}
  }
  if(s.study_state=="resume"||s.study_state=="complete")hit_targets_.push_back({72,522,220,56,"study.next"});
  if(s.can_back){label(screen,"Back",776,103,100,&lv_font_montserrat_14,0xdbe5df);hit_targets_.push_back({754,86,94,44,"study.back"});}
  if(s.can_undo){label(screen,"Undo",894,103,100,&lv_font_montserrat_14,0xe9c480);hit_targets_.push_back({866,86,118,44,"study.rating.undo"});}
 } else {
  const auto &state=s.agenda_state;
  if(state=="today" || state=="stale") {
   label(content,"A little space for today",32,26,850,&lv_font_montserrat_32,0xf4f0e8);
   label(content,"BIRTHDAYS  /  TODAY + NEXT 7 DAYS",32,85,850,&lv_font_montserrat_14,0x8db5a7);
   label(content,"Rowan     Today\nAlex          Sunday, September 27",32,116,840,&lv_font_montserrat_24,0xf4f0e8);
   label(content,"YOUR DAY",32,220,850,&lv_font_montserrat_14,0x8db5a7);
   label(content,"All day      A day for making\n09:30        Garden planning\n14:00        Busy",32,250,840,&lv_font_montserrat_24,0xf4f0e8);
   label(content,state=="stale" ? "STALE FIXTURE - reconnect to refresh" : "Synthetic people and dates. No connected accounts.",32,402,850,&lv_font_montserrat_14,0xe9c480);
  } else {
   std::string title,detail;
   if(state=="expired"){title="Agenda snapshot expired";detail="Prior-day and hard-expired details are hidden.";}
   else if(state=="composer"){title="Ready to record a draft";detail="A prepare request has no capture or external effect.\nRecording is unavailable in this fixture-only surface.";}
   else if(state=="recording"){title="Recording preview  /  00:12 of 01:00";detail="SIMULATED STATE - microphone is closed.\nLexi, finish recording\nA reachable companion and a separate Record touch are required.";}
   else if(state=="text_preview"){title="Review your draft";detail="Happy birthday, Rowan. Wishing you a wonderful day.\n\nSynthetic text preview. Audio submission is disabled.";}
   else if(state=="confirmation"){title="Draft only - no submission";detail="A separate qualifying touch hold would be required.\nNo Messages adapter, recipient route or capture lease exists.";}
   else if(state=="delivery_unknown"){title="Delivery unknown";detail="Do not retry automatically. Reconcile with the sender.\nThis is a synthetic outcome; nothing was sent.";}
   else {title="Companion unavailable";detail="Recording cannot begin without the signed, reachable companion.\nNo offline personal drafts are retained.";}
   label(content,title,32,50,850,&lv_font_montserrat_32,0xf4f0e8);
   label(content,detail,32,128,850,&lv_font_montserrat_24,0xaab8b7);
   label(content,"FIXTURE ONLY  /  Draft only  /  Text only",32,402,850,&lv_font_montserrat_14,0xe9c480);
  }
 }
 if(s.surface_id=="study"&&!s.delivery_status.empty())label(screen,s.delivery_status,280,103,450,&lv_font_montserrat_14,0xe9c480);
 if(!s.acknowledgement.empty())label(screen,s.acknowledgement,376,26,460,&lv_font_montserrat_14,0xe9c480);
 if(s.lifecycle.presence_state==bhp::panel_mode_protocol::PresenceState::GracePeriod) {
  auto *dim=box(screen,0,0,width,height,0);lv_obj_set_style_bg_opa(dim,LV_OPA_50,0);
 }
}
}
