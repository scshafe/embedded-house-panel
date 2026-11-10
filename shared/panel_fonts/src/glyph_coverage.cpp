#include "panel_fonts/glyph_coverage.hpp"

#include "glyph_ranges.inc"
#include "clock_glyph_ranges.inc"

namespace bhp::panel_fonts {

bool has_glyph(const std::uint32_t code_point, const FontSubset subset) noexcept
{
    if (subset == FontSubset::Clock) {
        for (const auto &range : clock_glyph_ranges) {
            if (code_point >= range[0] && code_point <= range[1]) return true;
        }
        return false;
    }
    if (subset != FontSubset::Latin) return false;
    for (const auto &range : glyph_ranges) {
        if (code_point >= range[0] && code_point <= range[1]) return true;
    }
    return false;
}

namespace {
constexpr std::uint32_t malformed = 0xffffffffU;

// Decodes the scalar at `offset`; `length` is how many bytes it (or the malformed lead) spans.
std::uint32_t decode(const std::string_view utf8, const std::size_t offset, std::size_t &length) noexcept
{
    const auto lead = static_cast<unsigned char>(utf8[offset]);
    std::uint32_t scalar = lead;
    std::uint32_t minimum = 0;
    length = 1;
    if (lead >= 0xc2 && lead <= 0xdf) {
        length = 2; scalar = lead & 0x1f; minimum = 0x80;
    } else if (lead >= 0xe0 && lead <= 0xef) {
        length = 3; scalar = lead & 0x0f; minimum = 0x800;
    } else if (lead >= 0xf0 && lead <= 0xf4) {
        length = 4; scalar = lead & 0x07; minimum = 0x10000;
    } else if (lead >= 0x80) {
        return malformed;
    }
    if (length > utf8.size() - offset) { length = 1; return malformed; }
    for (std::size_t i = 1; i < length; ++i) {
        const auto continuation = static_cast<unsigned char>(utf8[offset + i]);
        if ((continuation & 0xc0) != 0x80) { length = 1; return malformed; }
        scalar = (scalar << 6) | (continuation & 0x3f);
    }
    if (scalar < minimum || scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff)) return malformed;
    return scalar;
}

bool drawable(const std::uint32_t scalar, const FontSubset subset) noexcept
{
    if (scalar == malformed) return false;
    for (const auto control : layout_controls) {
        if (subset == FontSubset::Latin && control == scalar) return true;
    }
    return has_glyph(scalar, subset);
}
} // namespace

GlyphCoverageResult validate_glyph_coverage(const std::string_view utf8, const FontSubset subset) noexcept
{
    std::size_t offset = 0;
    while (offset < utf8.size()) {
        std::size_t length = 1;
        const auto scalar = decode(utf8, offset, length);
        if (scalar == malformed) return {false, offset, malformed};
        if (!drawable(scalar, subset)) return {false, offset, scalar};
        offset += length;
    }
    return {true, utf8.size(), 0};
}

FittedText fit_to_subset(const std::string_view utf8, const std::size_t maximum_code_points, const FontSubset subset)
{
    FittedText fitted{{}, false, false};
    const bool latin = subset == FontSubset::Latin;
    std::size_t offset = 0;
    for (std::size_t count = 0; offset < utf8.size(); ++count) {
        if (count == maximum_code_points) {
            fitted.truncated = true;
            if (latin) fitted.text += "\xE2\x80\xA6";
            break;
        }
        std::size_t length = 1;
        const auto scalar = decode(utf8, offset, length);
        if (drawable(scalar, subset)) {
            fitted.text.append(utf8, offset, length);
        } else {
            fitted.replaced = true;
            if (has_glyph('?', subset)) fitted.text += '?';
        }
        offset += length;
    }
    return fitted;
}

} // namespace bhp::panel_fonts
