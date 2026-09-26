#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace bhp::panel_fonts {

struct GlyphCoverageResult {
    bool supported;
    std::size_t byte_offset;
    std::uint32_t code_point;
};

enum class FontSubset { Latin, Clock };

// @canonical panel_font_glyph_coverage
// Returns whether the selected committed font subset contains the scalar.
// Latin covers 24/28/36/40/48px; Clock covers only digits and colon at 192px.
// LF is a Latin layout control, not a glyph. Unknown subsets return false.
bool has_glyph(std::uint32_t code_point, FontSubset subset = FontSubset::Latin) noexcept;

// Check text after the canonical shell Unicode policy has enforced its budgets,
// UTF-8 validity and control policy. No normalization or glyph fallback occurs.
// Failure identifies the first unsupported scalar and byte offset without text.
// Malformed UTF-8 defensively returns supported=false, code_point=0xFFFFFFFF.
// Success returns {true, utf8.size(), 0}; LF is accepted only for Latin.
// This checks font coverage, not clock-format validity or trusted time.
GlyphCoverageResult validate_glyph_coverage(std::string_view utf8,
                                          FontSubset subset = FontSubset::Latin) noexcept;

} // namespace bhp::panel_fonts
