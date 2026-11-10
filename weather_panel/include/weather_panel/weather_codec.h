#pragma once

#include "weather_panel/weather_models.h"

#include <string>
#include <vector>

/**
 * @canonical weather_codec -- strict parsers for the three pinned answers, and the display rules.
 *
 * Each parser rejects a missing field, a wrong type, an out-of-range value or an array of the
 * wrong length with WEATHER.<ANSWER>_INVALID.<FIELD>, and never guesses a value.
 */
namespace weather_panel
{
/** Open-Meteo's daily forecast (forecast_days=1). `fetched_at` is set by the caller. */
WeatherResult<DailyForecast> parse_forecast(const std::string &body);
/** Open-Meteo's place search: up to five places; an answer with no `results` key is no match. */
WeatherResult<std::vector<Place>> parse_search(const std::string &body);
/** free.freeipapi.com: the city and its coordinates (its `timeZones` lists the whole country). */
WeatherResult<ApproximateLocation> parse_location(const std::string &body);

/** Rounds to 0.01 degrees (about 1 km): every request and every write uses this. */
double round_coordinate(double degrees);

/** "72°F", "22°C" or "295 K", from a Celsius value; Kelvin has no degree sign. */
std::string temperature_text(double celsius, TemperatureUnit unit);
/** "↑ 7:12" or "↓ 18:21" from a local HH:MM. */
std::string sunrise_text(const std::string &local_time);
std::string sunset_text(const std::string &local_time);

/** The place's local date (YYYY-MM-DD) at `now`, judged with the forecast's UTC offset. */
std::string local_date(std::int64_t now, std::int64_t utc_offset_seconds);
Freshness freshness(const DailyForecast &forecast, std::int64_t now);

/** The settings and the cached forecast as the store writes them; parsing is as strict. */
std::string encode_settings(const WeatherSettings &settings);
WeatherResult<WeatherSettings> decode_settings(const std::string &text);
std::string encode_forecast(const DailyForecast &forecast);
WeatherResult<DailyForecast> decode_forecast(const std::string &text);
} // namespace weather_panel
