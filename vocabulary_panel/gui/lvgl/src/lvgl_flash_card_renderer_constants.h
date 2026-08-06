#pragma once

#include <cstdint>

namespace vocabulary_panel::gui::lvgl::constants {

inline constexpr std::uint32_t black_rgb888 = 0x000000;
inline constexpr std::uint32_t day_background_rgb888 = 0x101418;
inline constexpr std::uint32_t card_background_rgb888 = 0x18212b;
inline constexpr std::uint32_t card_pressed_rgb888 = 0x263545;
inline constexpr std::uint32_t primary_text_rgb888 = 0xf3f5f7;
inline constexpr std::uint32_t secondary_text_rgb888 = 0xaeb8c2;
inline constexpr std::uint32_t border_rgb888 = 0x526170;
inline constexpr std::uint32_t degraded_rgb888 = 0xffb74d;
inline constexpr std::uint32_t error_rgb888 = 0xff6b6b;
inline constexpr std::uint32_t again_rgb888 = 0x8f2d2d;
inline constexpr std::uint32_t hard_rgb888 = 0x865d1f;
inline constexpr std::uint32_t good_rgb888 = 0x245f45;
inline constexpr std::uint32_t easy_rgb888 = 0x24556e;

inline constexpr std::int32_t outer_margin_px = 12;
inline constexpr std::int32_t header_height_px = 24;
inline constexpr std::int32_t content_top_px = 36;
inline constexpr std::int32_t bottom_margin_px = 12;
inline constexpr std::int32_t button_gap_px = 6;
inline constexpr std::int32_t button_padding_px = 8;
inline constexpr std::int32_t rating_button_height_px = 82;
inline constexpr std::int32_t retry_button_width_px = 168;
inline constexpr std::int32_t retry_button_height_px = 64;

} // namespace vocabulary_panel::gui::lvgl::constants
