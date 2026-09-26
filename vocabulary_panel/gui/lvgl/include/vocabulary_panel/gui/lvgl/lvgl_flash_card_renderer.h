#pragma once
#include <lvgl.h>
#include <string>
#include <vector>

namespace vocabulary_panel::gui::lvgl {
/** Validated presentation supplied by the shell from the canonical study reducer. */
struct VocabularySurfaceModel {
 std::string state;
 std::string word;
 std::string definition;
 std::string example;
 bool provisional{false};
 bool can_back{false};
 bool can_undo{false};
};
/** @canonical lvgl_flash_card_renderer -- subordinate vocabulary widgets only.
 * The shell owns LVGL, display commits, input hit testing and Unicode validation.
 * This function opens no device and owns no feature state, timers or provenance.
 */
void render_vocabulary_surface(lv_obj_t *parent, const VocabularySurfaceModel &model);
} // namespace vocabulary_panel::gui::lvgl
