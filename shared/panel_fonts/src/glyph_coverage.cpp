#include "panel_fonts/glyph_coverage.hpp"

#include "glyph_ranges.inc"

namespace bhp::panel_fonts {

bool has_glyph(const std::uint32_t code_point) noexcept
{
    for (const auto &range : glyph_ranges) {
        if (code_point >= range[0] && code_point <= range[1]) return true;
    }
    return false;
}

GlyphCoverageResult validate_glyph_coverage(const std::string_view utf8) noexcept
{
    std::size_t offset = 0;
    while (offset < utf8.size()) {
        const auto lead = static_cast<unsigned char>(utf8[offset]);
        std::size_t length = 1;
        std::uint32_t scalar = lead;
        std::uint32_t minimum = 0;
        if (lead >= 0xc2 && lead <= 0xdf) {
            length = 2; scalar = lead & 0x1f; minimum = 0x80;
        } else if (lead >= 0xe0 && lead <= 0xef) {
            length = 3; scalar = lead & 0x0f; minimum = 0x800;
        } else if (lead >= 0xf0 && lead <= 0xf4) {
            length = 4; scalar = lead & 0x07; minimum = 0x10000;
        } else if (lead >= 0x80) {
            return {false, offset, 0xffffffffU};
        }
        if (length > utf8.size() - offset) return {false, offset, 0xffffffffU};
        for (std::size_t i = 1; i < length; ++i) {
            const auto continuation = static_cast<unsigned char>(utf8[offset + i]);
            if ((continuation & 0xc0) != 0x80) return {false, offset, 0xffffffffU};
            scalar = (scalar << 6) | (continuation & 0x3f);
        }
        if (scalar < minimum || scalar > 0x10ffff ||
            (scalar >= 0xd800 && scalar <= 0xdfff)) return {false, offset, 0xffffffffU};
        bool is_layout_control = false;
        for (const auto control : layout_controls) {
            if (control == scalar) is_layout_control = true;
        }
        if (!is_layout_control && !has_glyph(scalar)) return {false, offset, scalar};
        offset += length;
    }
    return {true, utf8.size(), 0};
}

} // namespace bhp::panel_fonts
