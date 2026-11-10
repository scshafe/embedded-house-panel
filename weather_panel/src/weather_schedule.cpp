#include "weather_panel/weather_schedule.h"

#include <algorithm>
#include <cmath>

namespace weather_panel
{
WeatherSchedule::WeatherSchedule(std::function<double()> random01) : random01_(std::move(random01)) {}

std::int64_t WeatherSchedule::delay(unsigned failures) const
{
	std::int64_t seconds = kFirstRetrySeconds;
	for (unsigned step = 1; step < failures && seconds < kLastRetrySeconds; ++step)
		seconds *= 2;
	seconds = std::min(seconds, kLastRetrySeconds);
	const double spread = std::clamp(random01_ ? random01_() : 0.0, 0.0, 1.0) * 0.2;
	return std::max<std::int64_t>(1, static_cast<std::int64_t>(std::llround(seconds * (1.0 - spread))));
}

void WeatherSchedule::settings_changed(std::int64_t now, bool automatic)
{
	next_forecast_ = now;
	forecast_failures_ = 0;
	if (automatic && !location_known_) {
		next_location_ = now;
		location_failures_ = 0;
	}
}

bool WeatherSchedule::location_due(std::int64_t now, bool automatic) const
{
	return automatic && now >= next_location_;
}

bool WeatherSchedule::forecast_due(std::int64_t now, bool date_moved) const
{
	// A new local date fetches at once, unless the source is failing and backing off.
	return now >= next_forecast_ || (date_moved && forecast_failures_ == 0);
}

void WeatherSchedule::location_succeeded(std::int64_t now)
{
	location_known_ = true;
	location_failures_ = 0;
	next_location_ = now + kLocationSeconds;
}

void WeatherSchedule::location_failed(std::int64_t now) { next_location_ = now + delay(++location_failures_); }

void WeatherSchedule::forecast_succeeded(std::int64_t now)
{
	forecast_failures_ = 0;
	next_forecast_ = now + kRefreshSeconds;
}

void WeatherSchedule::forecast_failed(std::int64_t now) { next_forecast_ = now + delay(++forecast_failures_); }
} // namespace weather_panel
