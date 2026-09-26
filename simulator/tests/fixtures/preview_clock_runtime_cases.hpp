#pragma once
#include <array>
#include <chrono>

namespace bhp::simulator::fixtures {
struct PreviewClockPixelCase {
 const char *start_scenario;
 const char *midpoint_scenario;
 const char *settled_scenario;
 int left, top, right, bottom;
};
inline constexpr std::array<PreviewClockPixelCase,2> preview_clock_pixel_cases{{
 {"day-reel-000", "day-reel-200", "day-reel-400", 430, 0, 592, 68},
 {"night-reel-000", "night-reel-200", "night-reel-400", 230, 225, 794, 375}
}};
inline constexpr const char *preview_clock_day_scenario = "day-front";
inline constexpr const char *preview_clock_night_scenario = "night";
inline constexpr const char *preview_clock_day_start = "07:42";
inline constexpr const char *preview_clock_day_successor = "07:43";
inline constexpr std::array<const char *,3> preview_clock_cancel_modes{{"blank", "night", "day"}};
inline constexpr auto preview_clock_poll_interval = std::chrono::milliseconds(10);
inline constexpr auto preview_clock_settle_budget = std::chrono::milliseconds(1500);
inline constexpr auto preview_clock_observe_stable = std::chrono::milliseconds(100);
inline constexpr auto preview_clock_observe_frozen = std::chrono::milliseconds(450);
inline constexpr int preview_clock_frame_width = 1024;
inline constexpr int preview_clock_frame_height = 600;
}
