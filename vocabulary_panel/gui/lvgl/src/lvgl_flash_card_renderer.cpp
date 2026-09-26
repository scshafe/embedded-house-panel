#include "vocabulary_panel/gui/lvgl/lvgl_flash_card_renderer.h"
#include <cstdint>

namespace vocabulary_panel::gui::lvgl {
namespace {
void label(lv_obj_t *parent, const std::string &text, int x, int y, int width,
           const lv_font_t *font, std::uint32_t color)
{
 auto *object=lv_label_create(parent);
 lv_label_set_text(object,text.c_str());
 lv_label_set_long_mode(object,LV_LABEL_LONG_MODE_WRAP);
 lv_obj_set_pos(object,x,y); lv_obj_set_width(object,width);
 lv_obj_set_style_text_font(object,font,0);
 lv_obj_set_style_text_color(object,lv_color_hex(color),0);
}
void button(lv_obj_t *parent,const std::string &text,int x,int width,std::uint32_t color)
{
 auto *object=lv_obj_create(parent); lv_obj_remove_style_all(object);
 lv_obj_set_pos(object,x,392); lv_obj_set_size(object,width,56);
 lv_obj_set_style_bg_color(object,lv_color_hex(color),0);
 lv_obj_set_style_bg_opa(object,LV_OPA_COVER,0);
 lv_obj_set_style_radius(object,12,0);
 label(object,text,16,16,width-24,&lv_font_montserrat_20,0xf7f3eb);
}
}
void render_vocabulary_surface(lv_obj_t *parent,const VocabularySurfaceModel &model)
{
 if(model.state=="loading" || model.state=="empty" || model.state=="error" ||
    model.state=="refill" || model.state=="complete" || model.state=="resume" || model.state=="tracking_paused") {
  std::string title,detail;
  if(model.state=="loading") {title="Connecting to Lexinet";detail="Loading your next study session.";}
  if(model.state=="empty") {title="All caught up";detail="No review cards are available right now.";}
  if(model.state=="error") {title="Lexinet unavailable";detail="LEXINET.FIXTURE_UNAVAILABLE\nYour local study history is retained.";}
  if(model.state=="refill") {title="Offline - refill required";detail="Your rating is queued locally. Reconnect for another card.";}
  if(model.state=="complete") {title="Session complete";detail="Your rating is retained. Undo it, or continue when the next session is ready.";}
  if(model.state=="resume") {title="Ready when you are";detail="Touch Resume to return to your saved session.";}
  if(model.state=="tracking_paused") {title="Study tracking paused";detail="Rating not queued. Free some outbox capacity and retry.";}
  label(parent,title,32,100,850,&lv_font_montserrat_32,0xf4f0e8);
  label(parent,detail,32,168,850,&lv_font_montserrat_20,0xaab8b7);
  if(model.state=="resume") button(parent,"Resume",32,220,0x376662);
  if(model.state=="complete") button(parent,"Continue",32,220,0x376662);
  return;
 }
 label(parent,model.state=="back" ? "THE MEANING" : "A WORD TO KEEP",32,28,760,&lv_font_montserrat_14,0xa5c6bc);
 label(parent,model.word,32,72,850,&lv_font_montserrat_48,0xf4f0e8);
 if(model.state=="back") {
  label(parent,model.definition,32,154,820,&lv_font_montserrat_24,0xf4f0e8);
  label(parent,model.example,32,236,820,&lv_font_montserrat_20,0xaab8b7);
  button(parent,"Again",32,190,0x694044); button(parent,"Hard",238,190,0x67543a);
  button(parent,"Good",444,190,0x376662); button(parent,"Easy",650,190,0x3a526c);
 } else {
  label(parent,"A small discovery. A lasting idea.",32,154,800,&lv_font_montserrat_24,0xaab8b7);
  label(parent,"Touch the card to turn it over",32,286,800,&lv_font_montserrat_20,0xd7ded7);
  button(parent,"Flip card",32,220,0x376662); button(parent,"Next",650,190,0x293f43);
 }
 if(model.provisional)
  label(parent,"Voice preview - touch or rate to keep this observation",32,342,840,&lv_font_montserrat_14,0xe9c480);
}
} // namespace vocabulary_panel::gui::lvgl
