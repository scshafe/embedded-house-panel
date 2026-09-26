#include "panel_fonts/panel_fonts.h"
#include "fixtures/glyph_coverage_cases.hpp"

#include <iostream>

int main()
{
    lv_init();
    const lv_font_t *fonts[] = {&bhp_font_24, &bhp_font_28, &bhp_font_36, &bhp_font_40, &bhp_font_48};
    for (const auto *font : fonts) {
        if (font->fallback != nullptr) {
            std::cerr << "project_font_has_unreviewed_fallback\n";
            return 1;
        }
        for (std::uint32_t scalar = 0; scalar <= 0x2122; ++scalar) {
            if (!bhp::panel_fonts::has_glyph(scalar)) continue;
            lv_font_glyph_dsc_t glyph{};
            if (!lv_font_get_glyph_dsc(font, &glyph, scalar, 0) || glyph.is_placeholder ||
                glyph.resolved_font != font) {
                std::cerr << "manifest_glyph_missing_from_font: " << scalar << '\n';
                return 1;
            }
            // Space/NBSP may be empty by design; every other supported scalar
            // must have visible bitmap area, not a successful blank substitute.
            if (scalar != 0x20 && scalar != 0xa0 && (glyph.box_w == 0 || glyph.box_h == 0)) {
                std::cerr << "manifest_glyph_is_unexpectedly_blank: " << scalar << '\n';
                return 1;
            }
        }
        for (const auto scalar : font_fixtures::unsupported_scalars) {
            lv_font_glyph_dsc_t glyph{};
            if (lv_font_get_glyph_dsc(font, &glyph, scalar, 0)) {
                std::cerr << "unsupported_script_rendered_as_placeholder\n";
                return 1;
            }
        }
    }
    lv_deinit();
    std::cout << "{\"ok\":true,\"test\":\"panel_font_assets\",\"sizes\":5}\n";
    return 0;
}
