#include "weather_panel/weather_codec.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdio>
#include <regex>

namespace weather_panel
{
namespace
{
using Json = nlohmann::json;

WeatherError invalid(const std::string &answer, const std::string &field)
{
	return {"WEATHER." + answer + "_INVALID." + field, "The " + answer + " answer is malformed", false};
}

bool is_number(const Json &value) { return value.is_number() && !value.is_boolean(); }

bool in_range(const Json &value, double low, double high)
{
	if (!is_number(value))
		return false;
	const double number = value.get<double>();
	return std::isfinite(number) && number >= low && number <= high;
}

bool short_text(const Json &value, std::size_t maximum)
{
	return value.is_string() && !value.get<std::string>().empty() && value.get<std::string>().size() <= maximum &&
	       value.get<std::string>().find_first_of(std::string("\0\n\r\t", 4)) == std::string::npos;
}

const std::regex kDate(R"(\d{4}-(0[1-9]|1[0-2])-(0[1-9]|[12]\d|3[01]))");
const std::regex kLocalTime(R"(([01]\d|2[0-3]):[0-5]\d)");
const std::regex kLocalDateTime(R"((\d{4}-\d{2}-\d{2})T(([01]\d|2[0-3]):[0-5]\d))");
const std::regex kTimezone(R"([A-Za-z_]+(/[A-Za-z0-9_+-]+){0,2})");

double one_decimal(double value) { return std::round(value * 10.0) / 10.0; }

std::string rounded_integer(double value)
{
	const long rounded = std::lround(value);
	return std::to_string(rounded == 0 ? 0L : rounded); // never "-0"
}

std::string without_leading_zero(const std::string &local_time)
{
	if (!std::regex_match(local_time, kLocalTime))
		return "—";
	return local_time[0] == '0' ? local_time.substr(1) : local_time;
}

/** Days since 1970-01-01 to a civil date (Howard Hinnant's algorithm). */
std::string civil_date(std::int64_t days)
{
	days += 719468;
	const std::int64_t era = (days >= 0 ? days : days - 146096) / 146097;
	const std::int64_t day_of_era = days - era * 146097;
	const std::int64_t year_of_era =
	    (day_of_era - day_of_era / 1460 + day_of_era / 36524 - day_of_era / 146096) / 365;
	const std::int64_t day_of_year = day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
	const std::int64_t month_index = (5 * day_of_year + 2) / 153;
	const std::int64_t day = day_of_year - (153 * month_index + 2) / 5 + 1;
	const std::int64_t month = month_index < 10 ? month_index + 3 : month_index - 9;
	const std::int64_t year = year_of_era + era * 400 + (month <= 2 ? 1 : 0);
	char text[64];
	std::snprintf(text, sizeof(text), "%04lld-%02lld-%02lld", static_cast<long long>(year),
		      static_cast<long long>(month), static_cast<long long>(day));
	return text;
}

std::int64_t floor_divide(std::int64_t value, std::int64_t divisor)
{
	return value >= 0 ? value / divisor : -((-value + divisor - 1) / divisor);
}

const char *unit_name(TemperatureUnit unit)
{
	return unit == TemperatureUnit::celsius ? "celsius" : unit == TemperatureUnit::kelvin ? "kelvin" : "fahrenheit";
}

Json place_json(const Place &place)
{
	return {{"name", place.name},
		{"region", place.region},
		{"country", place.country},
		{"latitude", round_coordinate(place.latitude)},
		{"longitude", round_coordinate(place.longitude)}};
}

/** A place as the store and the search hold it: a name, optional region and country, coordinates. */
std::optional<Place> place_from(const Json &value)
{
	if (!value.is_object() || !short_text(value.value("name", Json()), 80) ||
	    !in_range(value.value("latitude", Json()), -90, 90) || !in_range(value.value("longitude", Json()), -180, 180))
		return std::nullopt;
	Place place;
	place.name = value["name"].get<std::string>();
	for (const auto &[key, target] : {std::pair<const char *, std::string *>{"region", &place.region},
					  std::pair<const char *, std::string *>{"country", &place.country}}) {
		if (!value.contains(key))
			continue;
		if (!value[key].is_string() || value[key].get<std::string>().size() > 80)
			return std::nullopt;
		*target = value[key].get<std::string>();
	}
	place.latitude = round_coordinate(value["latitude"].get<double>());
	place.longitude = round_coordinate(value["longitude"].get<double>());
	return place;
}

std::optional<Json> parse_object(const std::string &body)
{
	if (body.size() > 65536)
		return std::nullopt;
	Json root = Json::parse(body, nullptr, false);
	if (root.is_discarded() || !root.is_object())
		return std::nullopt;
	return root;
}
} // namespace

double round_coordinate(double degrees) { return std::round(degrees * 100.0) / 100.0; }

WeatherResult<DailyForecast> parse_forecast(const std::string &body)
{
	using Output = WeatherResult<DailyForecast>;
	const auto root = parse_object(body);
	if (!root)
		return Output::failure(invalid("FORECAST", "JSON"));
	const Json &json = *root;
	if (!json.contains("timezone") || !json["timezone"].is_string() ||
	    json["timezone"].get<std::string>().size() > 64 ||
	    !std::regex_match(json["timezone"].get<std::string>(), kTimezone))
		return Output::failure(invalid("FORECAST", "TIMEZONE"));
	if (!json.contains("utc_offset_seconds") || !json["utc_offset_seconds"].is_number_integer() ||
	    std::llabs(json["utc_offset_seconds"].get<long long>()) > 18 * 3600)
		return Output::failure(invalid("FORECAST", "UTC_OFFSET"));
	if (!json.contains("daily_units") || !json["daily_units"].is_object() ||
	    json["daily_units"].value("temperature_2m_max", "") != "°C" ||
	    json["daily_units"].value("temperature_2m_min", "") != "°C")
		return Output::failure(invalid("FORECAST", "UNITS"));
	if (!json.contains("daily") || !json["daily"].is_object())
		return Output::failure(invalid("FORECAST", "DAILY"));
	const Json &daily = json["daily"];
	for (const char *key : {"time", "temperature_2m_max", "temperature_2m_min", "sunrise", "sunset"})
		if (!daily.contains(key) || !daily[key].is_array() || daily[key].size() != 1)
			return Output::failure(invalid("FORECAST", "DAILY"));
	if (!daily["time"][0].is_string() || !std::regex_match(daily["time"][0].get<std::string>(), kDate))
		return Output::failure(invalid("FORECAST", "DATE"));
	if (!in_range(daily["temperature_2m_max"][0], -100, 70))
		return Output::failure(invalid("FORECAST", "HIGH"));
	if (!in_range(daily["temperature_2m_min"][0], -100, 70) ||
	    daily["temperature_2m_min"][0].get<double>() > daily["temperature_2m_max"][0].get<double>())
		return Output::failure(invalid("FORECAST", "LOW"));
	DailyForecast forecast;
	forecast.local_date = daily["time"][0].get<std::string>();
	for (const auto &[key, target, field] :
	     {std::tuple<const char *, std::string *, const char *>{"sunrise", &forecast.sunrise, "SUNRISE"},
	      std::tuple<const char *, std::string *, const char *>{"sunset", &forecast.sunset, "SUNSET"}}) {
		std::smatch match;
		const std::string text = daily[key][0].is_string() ? daily[key][0].get<std::string>() : "";
		if (!std::regex_match(text, match, kLocalDateTime) || match[1] != forecast.local_date)
			return Output::failure(invalid("FORECAST", field));
		*target = match[2];
	}
	forecast.timezone = json["timezone"].get<std::string>();
	forecast.utc_offset_seconds = json["utc_offset_seconds"].get<std::int64_t>();
	forecast.high_celsius = one_decimal(daily["temperature_2m_max"][0].get<double>());
	forecast.low_celsius = one_decimal(daily["temperature_2m_min"][0].get<double>());
	return Output::success(std::move(forecast));
}

WeatherResult<std::vector<Place>> parse_search(const std::string &body)
{
	using Output = WeatherResult<std::vector<Place>>;
	const auto root = parse_object(body);
	if (!root)
		return Output::failure(invalid("SEARCH", "JSON"));
	std::vector<Place> places;
	if (!root->contains("results"))
		return Output::success(places); // Open-Meteo sends no `results` key when nothing matches
	const Json &results = (*root)["results"];
	if (!results.is_array())
		return Output::failure(invalid("SEARCH", "RESULTS"));
	for (const Json &result : results) {
		if (places.size() == 5)
			break;
		if (!result.is_object())
			return Output::failure(invalid("SEARCH", "RESULTS"));
		Json shaped = {{"name", result.value("name", Json())},
			       {"latitude", result.value("latitude", Json())},
			       {"longitude", result.value("longitude", Json())}};
		if (result.contains("admin1"))
			shaped["region"] = result["admin1"];
		if (result.contains("country"))
			shaped["country"] = result["country"];
		const auto place = place_from(shaped);
		if (!place)
			return Output::failure(invalid("SEARCH", !short_text(shaped["name"], 80) ? "NAME" : "PLACE"));
		places.push_back(*place);
	}
	return Output::success(std::move(places));
}

WeatherResult<ApproximateLocation> parse_location(const std::string &body)
{
	using Output = WeatherResult<ApproximateLocation>;
	const auto root = parse_object(body);
	if (!root)
		return Output::failure(invalid("LOCATION", "JSON"));
	const Json &json = *root;
	if (!short_text(json.value("cityName", Json()), 80))
		return Output::failure(invalid("LOCATION", "CITY"));
	if (!in_range(json.value("latitude", Json()), -90, 90) || !in_range(json.value("longitude", Json()), -180, 180) ||
	    (json["latitude"].get<double>() == 0 && json["longitude"].get<double>() == 0))
		return Output::failure(invalid("LOCATION", "COORDINATES"));
	ApproximateLocation location;
	location.city = json["cityName"].get<std::string>();
	for (const auto &[key, target] : {std::pair<const char *, std::string *>{"regionName", &location.region},
					  std::pair<const char *, std::string *>{"countryName", &location.country}}) {
		if (!json.contains(key))
			continue;
		if (!json[key].is_string() || json[key].get<std::string>().size() > 80)
			return Output::failure(invalid("LOCATION", "REGION_OR_COUNTRY"));
		*target = json[key].get<std::string>();
	}
	location.latitude = round_coordinate(json["latitude"].get<double>());
	location.longitude = round_coordinate(json["longitude"].get<double>());
	return Output::success(std::move(location));
}

std::string temperature_text(double celsius, TemperatureUnit unit)
{
	if (unit == TemperatureUnit::kelvin)
		return rounded_integer(celsius + 273.15) + " K";
	if (unit == TemperatureUnit::celsius)
		return rounded_integer(celsius) + "°C";
	return rounded_integer(celsius * 9.0 / 5.0 + 32.0) + "°F";
}

std::string sunrise_text(const std::string &local_time) { return "↑ " + without_leading_zero(local_time); }
std::string sunset_text(const std::string &local_time) { return "↓ " + without_leading_zero(local_time); }

std::string local_date(std::int64_t now, std::int64_t utc_offset_seconds)
{
	return civil_date(floor_divide(now + utc_offset_seconds, 86400));
}

Freshness freshness(const DailyForecast &forecast, std::int64_t now)
{
	const std::int64_t age = now - forecast.fetched_at;
	// A forecast from the future means the clock moved; it cannot be judged, so it is not shown.
	if (age < -300 || age >= kUnavailableAfterSeconds ||
	    local_date(now, forecast.utc_offset_seconds) != forecast.local_date)
		return Freshness::unavailable;
	return age >= kStaleAfterSeconds ? Freshness::stale : Freshness::fresh;
}

std::string encode_settings(const WeatherSettings &settings)
{
	Json json = {{"schema", "bhp.weather-settings/v1"},
		     {"unit", unit_name(settings.unit)},
		     {"bar", settings.bar == BarContent::sun_times ? "sun_times" : "temperatures"}};
	if (settings.place)
		json["place"] = place_json(*settings.place);
	else
		json["location"] = "automatic";
	return json.dump(2) + "\n";
}

WeatherResult<WeatherSettings> decode_settings(const std::string &text)
{
	using Output = WeatherResult<WeatherSettings>;
	const auto root = parse_object(text);
	if (!root || root->value("schema", "") != "bhp.weather-settings/v1")
		return Output::failure({"WEATHER.STORE_INVALID", "The weather settings file is unreadable", false});
	const Json &json = *root;
	WeatherSettings settings;
	const std::string unit = json.value("unit", "");
	const std::string bar = json.value("bar", "");
	if (unit == "celsius")
		settings.unit = TemperatureUnit::celsius;
	else if (unit == "kelvin")
		settings.unit = TemperatureUnit::kelvin;
	else if (unit != "fahrenheit")
		return Output::failure({"WEATHER.STORE_INVALID", "The weather settings name no unit", false});
	if (bar == "sun_times")
		settings.bar = BarContent::sun_times;
	else if (bar != "temperatures")
		return Output::failure({"WEATHER.STORE_INVALID", "The weather settings name no bar", false});
	if (json.contains("place") == (json.value("location", "") == "automatic"))
		return Output::failure({"WEATHER.STORE_INVALID", "The weather settings name no location", false});
	if (json.contains("place")) {
		const auto place = place_from(json["place"]);
		if (!place)
			return Output::failure({"WEATHER.STORE_INVALID", "The stored place is malformed", false});
		settings.place = *place;
	}
	return Output::success(std::move(settings));
}

std::string encode_forecast(const DailyForecast &forecast)
{
	return Json{{"schema", "bhp.weather-forecast/v1"},
		    {"local_date", forecast.local_date},
		    {"timezone", forecast.timezone},
		    {"utc_offset_seconds", forecast.utc_offset_seconds},
		    {"high_celsius", forecast.high_celsius},
		    {"low_celsius", forecast.low_celsius},
		    {"sunrise", forecast.sunrise},
		    {"sunset", forecast.sunset},
		    {"fetched_at", forecast.fetched_at},
		    {"latitude", round_coordinate(forecast.latitude)},
		    {"longitude", round_coordinate(forecast.longitude)}}
		   .dump(2) +
	       "\n";
}

WeatherResult<DailyForecast> decode_forecast(const std::string &text)
{
	using Output = WeatherResult<DailyForecast>;
	const WeatherError unreadable{"WEATHER.STORE_INVALID", "The cached forecast is unreadable", false};
	const auto root = parse_object(text);
	if (!root || root->value("schema", "") != "bhp.weather-forecast/v1")
		return Output::failure(unreadable);
	const Json &json = *root;
	if (!json.value("local_date", Json()).is_string() ||
	    !std::regex_match(json["local_date"].get<std::string>(), kDate) ||
	    !json.value("timezone", Json()).is_string() ||
	    !std::regex_match(json["timezone"].get<std::string>(), kTimezone) ||
	    !json.value("utc_offset_seconds", Json()).is_number_integer() ||
	    !in_range(json.value("high_celsius", Json()), -100, 70) || !in_range(json.value("low_celsius", Json()), -100, 70) ||
	    !json.value("sunrise", Json()).is_string() || !std::regex_match(json["sunrise"].get<std::string>(), kLocalTime) ||
	    !json.value("sunset", Json()).is_string() || !std::regex_match(json["sunset"].get<std::string>(), kLocalTime) ||
	    !json.value("fetched_at", Json()).is_number_integer() ||
	    !in_range(json.value("latitude", Json()), -90, 90) || !in_range(json.value("longitude", Json()), -180, 180))
		return Output::failure(unreadable);
	DailyForecast forecast;
	forecast.local_date = json["local_date"].get<std::string>();
	forecast.timezone = json["timezone"].get<std::string>();
	forecast.utc_offset_seconds = json["utc_offset_seconds"].get<std::int64_t>();
	forecast.high_celsius = json["high_celsius"].get<double>();
	forecast.low_celsius = json["low_celsius"].get<double>();
	forecast.sunrise = json["sunrise"].get<std::string>();
	forecast.sunset = json["sunset"].get<std::string>();
	forecast.fetched_at = json["fetched_at"].get<std::int64_t>();
	forecast.latitude = round_coordinate(json["latitude"].get<double>());
	forecast.longitude = round_coordinate(json["longitude"].get<double>());
	return Output::success(std::move(forecast));
}
} // namespace weather_panel
