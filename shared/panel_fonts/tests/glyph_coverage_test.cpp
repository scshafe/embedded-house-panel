#include "panel_fonts/glyph_coverage.hpp"
#include "fixtures/glyph_coverage_cases.hpp"

#include <iostream>

int main()
{
    for (const auto text : font_fixtures::supported_text) {
        const auto result = bhp::panel_fonts::validate_glyph_coverage(text);
        if (!result.supported || result.byte_offset != text.size()) {
            std::cerr << "supported_latin_and_punctuation_rejected\n";
            return 1;
        }
    }
    for (const auto &fixture : font_fixtures::unsupported_text) {
        const auto result = bhp::panel_fonts::validate_glyph_coverage(fixture.text);
        if (result.supported || result.byte_offset != fixture.offset || result.code_point != fixture.scalar) {
            std::cerr << "unsupported_scalar_not_identified\n";
            return 1;
        }
    }
    for (const auto text : font_fixtures::malformed_text) {
        const auto result = bhp::panel_fonts::validate_glyph_coverage(text);
        if (result.supported || result.code_point != 0xffffffffU) {
            std::cerr << "malformed_utf8_bypassed_coverage_guard\n";
            return 1;
        }
    }
    for (const auto text : font_fixtures::supported_clock_text) {
        const auto result = bhp::panel_fonts::validate_glyph_coverage(text, bhp::panel_fonts::FontSubset::Clock);
        if (!result.supported || result.byte_offset != text.size()) {
            std::cerr << "supported_clock_glyph_rejected\n";
            return 1;
        }
    }
    for (const auto &fixture : font_fixtures::unsupported_clock_text) {
        const auto result = bhp::panel_fonts::validate_glyph_coverage(fixture.text, bhp::panel_fonts::FontSubset::Clock);
        if (result.supported || result.byte_offset != fixture.offset || result.code_point != fixture.scalar) {
            std::cerr << "clock_font_accepted_text_outside_its_subset\n";
            return 1;
        }
    }
    for (const auto text : font_fixtures::malformed_text) {
        const auto result = bhp::panel_fonts::validate_glyph_coverage(text, bhp::panel_fonts::FontSubset::Clock);
        if (result.supported || result.code_point != 0xffffffffU) {
            std::cerr << "malformed_utf8_bypassed_clock_coverage_guard\n";
            return 1;
        }
    }
    std::cout << "{\"ok\":true,\"test\":\"panel_font_glyph_coverage\"}\n";
    return 0;
}
