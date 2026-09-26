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
    if (bhp_clock_font_192.fallback != nullptr) {
        std::cerr << "clock_font_has_unreviewed_fallback\n";
        return 1;
    }
    std::uint32_t clock_glyph_count = 0;
    for (std::uint32_t scalar = 0x20; scalar <= 0x2122; ++scalar) {
        lv_font_glyph_dsc_t glyph{};
        const bool found = lv_font_get_glyph_dsc(&bhp_clock_font_192, &glyph, scalar, 0);
        const bool expected = bhp::panel_fonts::has_glyph(scalar, bhp::panel_fonts::FontSubset::Clock);
        if (found != expected || (found && (glyph.is_placeholder || glyph.box_w == 0 ||
                                           glyph.box_h == 0 || glyph.resolved_font != &bhp_clock_font_192))) {
            std::cerr << "clock_font_differs_from_its_manifest: " << scalar << '\n';
            return 1;
        }
        if (found) ++clock_glyph_count;
    }
    if (clock_glyph_count != 11) {
        std::cerr << "clock_font_glyph_count_differs\n";
        return 1;
    }
    for (const auto scalar : font_fixtures::unsupported_scalars) {
        lv_font_glyph_dsc_t glyph{};
        if (lv_font_get_glyph_dsc(&bhp_clock_font_192, &glyph, scalar, 0)) {
            std::cerr << "clock_font_rendered_unsupported_script\n";
            return 1;
        }
    }
    std::cout << "{\"ok\":true,\"test\":\"panel_font_assets\",\"latin_sizes\":5,"
              << "\"clock_glyphs\":" << clock_glyph_count
              << ",\"clock_line_height_px\":" << bhp_clock_font_192.line_height
              << ",\"clock_advance_px\":{\"digits\":[";
    for (std::uint32_t scalar = '0'; scalar <= '9'; ++scalar) {
        if (scalar != '0') std::cout << ',';
        std::cout << lv_font_get_glyph_width(&bhp_clock_font_192, scalar, 0);
    }
    std::cout << "],\"colon\":" << lv_font_get_glyph_width(&bhp_clock_font_192, ':', 0)
              << "},\"clock_text_width_px\":{";
    bool first = true;
    for (const auto text : font_fixtures::clock_metric_text) {
        if (!first) std::cout << ',';
        first = false;
        std::uint32_t width = 0;
        for (std::size_t i = 0; i < text.size(); ++i) {
            const auto next = i + 1 < text.size() ? static_cast<std::uint32_t>(text[i + 1]) : 0;
            width += lv_font_get_glyph_width(&bhp_clock_font_192, text[i], next);
        }
        std::cout << '\"' << text << "\":" << width;
    }
    std::cout << "}}\n";
    lv_deinit();
    return 0;
}
