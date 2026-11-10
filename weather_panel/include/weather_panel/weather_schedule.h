#pragma once

#include <cstdint>
#include <functional>

namespace weather_panel
{
/**
 * @canonical weather_schedule -- when to look up the location and fetch the forecast (ADR 0015).
 *
 * Times are monotonic seconds. The forecast refreshes every 60 min, at once on a settings
 * change, and when the place's local date moves on. The automatic location is looked up at
 * start and every 6 h, only while Automatic is selected. A failure backs off from 1 min,
 * doubling to 30 min, each delay shortened at random by up to a fifth; a success resets it.
 */
class WeatherSchedule final
{
      public:
	static constexpr std::int64_t kRefreshSeconds = 3600;
	static constexpr std::int64_t kLocationSeconds = 6 * 3600;
	static constexpr std::int64_t kFirstRetrySeconds = 60;
	static constexpr std::int64_t kLastRetrySeconds = 1800;

	explicit WeatherSchedule(std::function<double()> random01 = [] { return 0.0; });
	/** Fetch now; look the location up now if Automatic is selected and none is known. */
	void settings_changed(std::int64_t now, bool automatic);
	bool location_due(std::int64_t now, bool automatic) const;
	/** `date_moved` is true when the cached forecast is for an earlier local date. */
	bool forecast_due(std::int64_t now, bool date_moved) const;
	void location_succeeded(std::int64_t now);
	void location_failed(std::int64_t now);
	void forecast_succeeded(std::int64_t now);
	void forecast_failed(std::int64_t now);
	bool location_known() const { return location_known_; }
	std::int64_t next_forecast() const { return next_forecast_; }
	std::int64_t next_location() const { return next_location_; }

      private:
	std::int64_t delay(unsigned failures) const;
	std::function<double()> random01_;
	std::int64_t next_forecast_{0};
	std::int64_t next_location_{0};
	unsigned forecast_failures_{0};
	unsigned location_failures_{0};
	bool location_known_{false};
};
} // namespace weather_panel
