#pragma once

#include "panel_supervisor/brightness_levels.hpp"
#include "panel_supervisor/result.hpp"
#include "panel_supervisor/supervisor_config.hpp"

#include <chrono>

namespace house_panel::supervisor::test_fixture {

inline constexpr int kPresentDayBrightnessPercent = 90;
inline constexpr int kPresentNightBrightnessPercent = 30;
inline constexpr int kGraceDayBrightnessPercent = 50;
inline constexpr int kGraceNightBrightnessPercent = 20;
inline constexpr int kSuspendedBrightnessPercent = 0;
inline constexpr std::chrono::milliseconds kGracePeriod{10'000};
inline const std::chrono::system_clock::time_point kInitialTime{
	std::chrono::milliseconds(1'700'000'000'000LL)};

inline Result<SupervisorConfig> valid_config()
{
	return SupervisorConfig::create(kGracePeriod, BrightnessLevels{
							      kPresentDayBrightnessPercent,
							      kPresentNightBrightnessPercent,
							      kGraceDayBrightnessPercent,
							      kGraceNightBrightnessPercent,
							      kSuspendedBrightnessPercent,
						      });
}

} // namespace house_panel::supervisor::test_fixture
