#include "panel_fonts/glyph_coverage.hpp"
#include "fixtures/glyph_coverage_cases.hpp"

#include <iostream>
#include <string_view>

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
    // Live text is always made drawable: every case below passes coverage afterwards.
    struct FitCase { std::string_view input; std::size_t maximum; std::string_view text; bool replaced, truncated; };
    const FitCase fits[] = {
        {"caf\xC3\xA9 line\nbreak", 64, "caf\xC3\xA9 line\nbreak", false, false},
        {"\xE6\x97\xA5\xE6\x9C\xAC word", 64, "?? word", true, false},          // CJK has no glyph
        {"tab\there\x07", 64, "tab?here?", true, false},                           // controls other than LF
        {"bad \xC3 and \xF0\x9F\x98", 64, "bad ? and ???", true, false},          // malformed sequences
        {"serendipity", 5, "seren\xE2\x80\xA6", false, true},
        {"\xE6\x97\xA5\xE6\x9C\xAC" "abc", 2, "??\xE2\x80\xA6", true, true},
        {"", 4, "", false, false},
    };
    for (const auto &fit : fits) {
        const auto result = bhp::panel_fonts::fit_to_subset(fit.input, fit.maximum);
        if (result.text != fit.text || result.replaced != fit.replaced || result.truncated != fit.truncated ||
            !bhp::panel_fonts::validate_glyph_coverage(result.text).supported) {
            std::cerr << "live_text_fit_wrong\n";
            return 1;
        }
    }
    const auto clock = bhp::panel_fonts::fit_to_subset("12:3x45", 4, bhp::panel_fonts::FontSubset::Clock);
    if (clock.text != "12:3" || clock.replaced || !clock.truncated ||
        !bhp::panel_fonts::validate_glyph_coverage(clock.text, bhp::panel_fonts::FontSubset::Clock).supported) {
        std::cerr << "clock_text_fit_wrong\n";
        return 1;
    }
    std::cout << "{\"ok\":true,\"test\":\"panel_font_glyph_coverage\"}\n";
    return 0;
}
