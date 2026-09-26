#ifndef BHP_PANEL_FONTS_H
#define BHP_PANEL_FONTS_H

#include "lvgl.h"

#ifdef __cplusplus
#include "panel_fonts/glyph_coverage.hpp"
extern "C" {
#endif

// Generated OFL-1.1 project font assets; see glyph_manifest.json and fonts/OFL.txt.
extern const lv_font_t bhp_font_24;
extern const lv_font_t bhp_font_28;
extern const lv_font_t bhp_font_36;
extern const lv_font_t bhp_font_40;
extern const lv_font_t bhp_font_48;
// Clock-only subset: ASCII digits and colon, with no other Latin glyphs/fallback.
extern const lv_font_t bhp_clock_font_192;

#ifdef __cplusplus
}
#endif

#endif
