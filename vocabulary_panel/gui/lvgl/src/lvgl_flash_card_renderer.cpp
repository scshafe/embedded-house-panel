#include "vocabulary_panel/gui/lvgl/lvgl_flash_card_renderer.h"
#include "vocabulary_panel/gui/lvgl/vocabulary_surface_layout.h"
#include <panel_fonts/panel_fonts.h>
#include <cstdint>
#include <stdexcept>

namespace vocabulary_panel::gui::lvgl {
namespace {
void label(lv_obj_t *parent, const std::string &text, int x, int y, int width,
           int maximum_height, const lv_font_t *font, std::uint32_t color,
           lv_text_align_t alignment = LV_TEXT_ALIGN_LEFT)
{
 auto *object = lv_label_create(parent);
 lv_label_set_text(object, text.c_str());
 lv_label_set_long_mode(object, LV_LABEL_LONG_MODE_WRAP);
 lv_obj_set_pos(object, x, y);
 lv_obj_set_width(object, width);
 lv_obj_set_style_text_font(object, font, 0);
 lv_obj_set_style_text_color(object, lv_color_hex(color), 0);
 lv_obj_set_style_text_align(object, alignment, 0);
 lv_obj_set_style_text_line_space(object, 0, 0);
 lv_obj_update_layout(object);
 // A fixed type scale is part of the display contract. Do not silently clip,
 // ellipsize or shrink study content when its measured layout exceeds the slot.
 if(lv_obj_get_height(object) > maximum_height)
  throw std::runtime_error("PANEL_UI.TEXT_LAYOUT_OVERFLOW");
}
void button(lv_obj_t *parent, const std::string &text, int x, int width, std::uint32_t color)
{
 auto *object = lv_obj_create(parent);
 lv_obj_remove_style_all(object);
 lv_obj_set_pos(object, x, layout::actions_y);
 lv_obj_set_size(object, width, layout::action_height);
 lv_obj_set_style_bg_color(object, lv_color_hex(color), 0);
 lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
 lv_obj_set_style_radius(object, 12, 0);
 lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
 label(object, text, 12, (layout::action_height - lv_font_get_line_height(&bhp_font_28)) / 2,
       width - 24, lv_font_get_line_height(&bhp_font_28), &bhp_font_28, 0xf7f3eb, LV_TEXT_ALIGN_CENTER);
}
}
void render_vocabulary_surface(lv_obj_t *parent, const VocabularySurfaceModel &model)
{
 if(model.state == "loading" || model.state == "empty" || model.state == "error" ||
    model.state == "refill" || model.state == "complete" || model.state == "resume" || model.state == "tracking_paused") {
  std::string title, detail;
  if(model.state == "loading") { title = "Loading cards"; detail = "Connecting to Lexinet."; }
  if(model.state == "empty") { title = "All caught up"; detail = "No cards are ready to review."; }
  if(model.state == "error") { title = "Cards are unavailable"; detail = "Your local progress is kept on this panel."; }
  if(model.state == "refill") { title = "More cards when you reconnect"; detail = "Connect to load the next card."; }
  if(model.state == "complete") {
   title = "Session complete";
   detail = model.can_undo ? "You can undo your last rating or continue." : "Continue when the next session is ready.";
  }
  if(model.state == "resume") { title = "Ready when you are"; detail = "Resume your saved session."; }
  if(model.state == "tracking_paused") { title = "Study is paused"; detail = "Cannot save this review right now.\nYour card is unchanged."; }
  label(parent, title, layout::inset, 44, layout::text_width, 104, &bhp_font_40, 0xf4f0e8);
  label(parent, detail, layout::inset, 168, layout::text_width, 111, &bhp_font_28, 0xaab8b7);
  if(model.state == "resume") button(parent, "Resume", layout::inset, layout::primary_width, 0x376662);
  if(model.state == "complete") button(parent, "Continue", layout::inset, layout::primary_width, 0x376662);
  return;
 }
 if(model.state == "back") {
  label(parent, model.word, layout::inset, 16, layout::text_width, 62, &bhp_font_48, 0xf4f0e8);
  label(parent, model.definition, layout::inset, 90, layout::text_width, 132, &bhp_font_36, 0xf4f0e8);
  label(parent, model.example, layout::inset, 228, layout::text_width, 74, &bhp_font_28, 0xaab8b7);
  button(parent, "Again", layout::action_x(0), layout::action_width, 0x694044);
  button(parent, "Hard", layout::action_x(1), layout::action_width, 0x67543a);
  button(parent, "Good", layout::action_x(2), layout::action_width, 0x376662);
  button(parent, "Easy", layout::action_x(3), layout::action_width, 0x3a526c);
 } else {
  label(parent, model.word, layout::inset, 96, layout::text_width, 124, &bhp_font_48, 0xf4f0e8);
  button(parent, "Flip card", layout::inset, layout::primary_width, 0x376662);
  button(parent, "Next", layout::action_x(3), layout::action_width, 0x293f43);
 }
 if(model.provisional)
  label(parent, "Preview only. Touch or rate to keep.", layout::inset, 308, layout::text_width,
        31, &bhp_font_24, 0xe9c480);
}
} // namespace vocabulary_panel::gui::lvgl
