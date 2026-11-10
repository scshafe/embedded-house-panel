#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

/**
 * @canonical weather_models -- the daily forecast, its settings and the WEATHER.* errors
 * (ADR 0015, weather_panel/README.md). Nothing here invents a value: a field that is not in
 * a parsed answer is an error, never a default.
 */
namespace weather_panel
{
struct WeatherError {
	std::string code;
	std::string message;
	/** A transport failure or 429/5xx worth retrying after the backoff; never a parse failure. */
	bool retryable{false};
};

template <typename Value> class WeatherResult final
{
      public:
	static WeatherResult success(Value value) { return WeatherResult(std::move(value)); }
	static WeatherResult failure(WeatherError error) { return WeatherResult(std::move(error)); }
	bool has_value() const { return std::holds_alternative<Value>(storage_); }
	const Value &value() const
	{
		if (!has_value())
			throw std::logic_error("WEATHER.RESULT_VALUE_UNAVAILABLE");
		return std::get<Value>(storage_);
	}
	const WeatherError &error() const
	{
		if (has_value())
			throw std::logic_error("WEATHER.RESULT_ERROR_UNAVAILABLE");
		return std::get<WeatherError>(storage_);
	}

      private:
	explicit WeatherResult(Value value) : storage_(std::move(value)) {}
	explicit WeatherResult(WeatherError error) : storage_(std::move(error)) {}
	std::variant<Value, WeatherError> storage_;
};

enum class TemperatureUnit { fahrenheit, celsius, kelvin };
enum class BarContent { temperatures, sun_times };

/** A named place from the search; coordinates already rounded to 0.01 degrees. */
struct Place {
	std::string name;
	std::string region;
	std::string country;
	double latitude{0};
	double longitude{0};
	bool operator==(const Place &other) const
	{
		return name == other.name && region == other.region && country == other.country &&
		       latitude == other.latitude && longitude == other.longitude;
	}
};

/** ADR 0015 decision 5: degrees F, high/low and Automatic. */
struct WeatherSettings {
	TemperatureUnit unit{TemperatureUnit::fahrenheit};
	BarContent bar{BarContent::temperatures};
	/** Empty means Automatic (approximate, by IP). */
	std::optional<Place> place;
	bool operator==(const WeatherSettings &other) const
	{
		return unit == other.unit && bar == other.bar && place == other.place;
	}
};

/** The automatic location: city-level, from the pinned IP-geolocation service. */
struct ApproximateLocation {
	std::string city;
	std::string region;
	std::string country;
	double latitude{0};
	double longitude{0};
};

/** One day's forecast for one place, in that place's own time. */
struct DailyForecast {
	/** YYYY-MM-DD in the place's time zone. */
	std::string local_date;
	std::string timezone;
	/** The place's offset from UTC when it was fetched; dates are judged with it. */
	std::int64_t utc_offset_seconds{0};
	double high_celsius{0};
	double low_celsius{0};
	/** HH:MM, local to the place. */
	std::string sunrise;
	std::string sunset;
	/** When it was fetched, Unix seconds UTC. */
	std::int64_t fetched_at{0};
	/** The rounded coordinates it was fetched for, so it is never shown for another place. */
	double latitude{0};
	double longitude{0};
};

enum class Freshness { fresh, stale, unavailable };

/** ADR 0015: stale after 3 h; unavailable after 12 h or once the place's local date moved on. */
inline constexpr std::int64_t kStaleAfterSeconds = 3 * 3600;
inline constexpr std::int64_t kUnavailableAfterSeconds = 12 * 3600;
} // namespace weather_panel
