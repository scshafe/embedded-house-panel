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
 * Returns how many labels were cut to fit their slot (set_label_text_within).
 */
int render_vocabulary_surface(lv_obj_t *parent, const VocabularySurfaceModel &model);
/** @canonical panel_label_fit
 * Sets a wrapping label's text. Text taller than maximum_height is cut at a code
 * point and ends with U+2026, the visible marker, so it fits; nothing is
 * silently clipped or shrunk, and nothing throws. Returns true when it cut.
 */
bool set_label_text_within(lv_obj_t *label, const std::string &text, int maximum_height);
} // namespace vocabulary_panel::gui::lvgl
