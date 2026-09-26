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

// @canonical panel_font_glyph_coverage
// Returns whether the committed project-font manifest contains the scalar.
// LF is a layout control, not a glyph; all fonts have identical glyph coverage.
bool has_glyph(std::uint32_t code_point) noexcept;

// Check text after the canonical shell Unicode policy has enforced its budgets,
// UTF-8 validity and control policy. No normalization or glyph fallback occurs.
// Failure identifies the first unsupported scalar and byte offset without text.
// Malformed UTF-8 defensively returns supported=false, code_point=0xFFFFFFFF.
// Success returns {true, utf8.size(), 0}; LF is accepted as a layout control.
GlyphCoverageResult validate_glyph_coverage(std::string_view utf8) noexcept;

} // namespace bhp::panel_fonts
