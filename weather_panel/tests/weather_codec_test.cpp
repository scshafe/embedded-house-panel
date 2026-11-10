#include "weather_panel/weather_codec.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>

using namespace weather_panel;
using Json = nlohmann::json;

namespace
{
int failures = 0;
void expect(bool condition, const std::string &label)
{
	if (!condition) {
		++failures;
		std::cerr << "FAIL: " << label << '\n';
	}
}

std::string fixture(const std::string &name)
{
	std::ifstream input(std::string(WEATHER_FIXTURE_DIR) + "/" + name);
	std::ostringstream text;
	text << input.rdbuf();
	return text.str();
}

template <typename Parse>
void expect_refused(Parse parse, const Json &document, const std::string &code, const std::string &label)
{
	const auto result = parse(document.dump());
	expect(!result.has_value() && result.error().code == code,
	       label + " -> " + code + (result.has_value() ? " (accepted)" : " (got " + result.error().code + ")"));
}

void forecast_fixture_and_mutations()
{
	const auto parsed = parse_forecast(fixture("open-meteo-forecast-boulder.json"));
	expect(parsed.has_value(), "the recorded forecast parses");
	if (parsed.has_value()) {
		const auto &f = parsed.value();
		expect(f.local_date == "2026-11-10" && f.timezone == "America/Denver" && f.utc_offset_seconds == -25200,
		       "date, zone and offset");
		expect(f.high_celsius == 20.4 && f.low_celsius == 3.5, "high and low in Celsius");
		expect(f.sunrise == "06:40" && f.sunset == "16:48", "local sun times");
	}
	const Json base = Json::parse(fixture("open-meteo-forecast-boulder.json"));
	const auto parse = [](const std::string &body) { return parse_forecast(body); };
	auto mutated = [&](const std::function<void(Json &)> &change) {
		Json copy = base;
		change(copy);
		return copy;
	};
	expect(!parse_forecast("{").has_value() && parse_forecast("{").error().code == "WEATHER.FORECAST_INVALID.JSON",
	       "malformed JSON");
	expect_refused(parse, mutated([](Json &j) { j.erase("timezone"); }), "WEATHER.FORECAST_INVALID.TIMEZONE", "no zone");
	expect_refused(parse, mutated([](Json &j) { j["timezone"] = "../etc"; }), "WEATHER.FORECAST_INVALID.TIMEZONE",
		       "a path for a zone");
	expect_refused(parse, mutated([](Json &j) { j["utc_offset_seconds"] = "x"; }),
		       "WEATHER.FORECAST_INVALID.UTC_OFFSET", "offset type");
	expect_refused(parse, mutated([](Json &j) { j["daily_units"]["temperature_2m_max"] = "°F"; }),
		       "WEATHER.FORECAST_INVALID.UNITS", "Fahrenheit answer");
	expect_refused(parse, mutated([](Json &j) { j["daily"]["time"].push_back("2026-11-11"); }),
		       "WEATHER.FORECAST_INVALID.DAILY", "two days");
	expect_refused(parse, mutated([](Json &j) { j["daily"].erase("sunset"); }), "WEATHER.FORECAST_INVALID.DAILY",
		       "no sunset array");
	expect_refused(parse, mutated([](Json &j) { j["daily"]["time"][0] = "2026-13-01"; }),
		       "WEATHER.FORECAST_INVALID.DATE", "a bad date");
	expect_refused(parse, mutated([](Json &j) { j["daily"]["temperature_2m_max"][0] = "warm"; }),
		       "WEATHER.FORECAST_INVALID.HIGH", "high type");
	expect_refused(parse, mutated([](Json &j) { j["daily"]["temperature_2m_max"][0] = 99.0; }),
		       "WEATHER.FORECAST_INVALID.HIGH", "high out of range");
	expect_refused(parse, mutated([](Json &j) { j["daily"]["temperature_2m_min"][0] = 30.0; }),
		       "WEATHER.FORECAST_INVALID.LOW", "low above high");
	expect_refused(parse, mutated([](Json &j) { j["daily"]["temperature_2m_min"][0] = nullptr; }),
		       "WEATHER.FORECAST_INVALID.LOW", "null low");
	expect_refused(parse, mutated([](Json &j) { j["daily"]["sunrise"][0] = "06:40"; }),
		       "WEATHER.FORECAST_INVALID.SUNRISE", "sunrise without a date");
	expect_refused(parse, mutated([](Json &j) { j["daily"]["sunset"][0] = "2026-11-09T16:48"; }),
		       "WEATHER.FORECAST_INVALID.SUNSET", "sunset on another day");
}

void search_and_location()
{
	const auto found = parse_search(fixture("open-meteo-search-boulder.json"));
	expect(found.has_value() && found.value().size() == 5, "five places");
	if (found.has_value() && !found.value().empty()) {
		const auto &first = found.value().front();
		expect(first.name == "Boulder" && first.region == "Colorado" && first.country == "United States",
		       "the first place's name, region and country");
		expect(first.latitude == round_coordinate(first.latitude), "search coordinates arrive rounded");
	}
	const auto none = parse_search(fixture("open-meteo-search-none.json"));
	expect(none.has_value() && none.value().empty(), "no `results` key is no match, not an error");
	const Json base = Json::parse(fixture("open-meteo-search-boulder.json"));
	const auto parse = [](const std::string &body) { return parse_search(body); };
	Json copy = base;
	copy["results"] = "Boulder";
	expect_refused(parse, copy, "WEATHER.SEARCH_INVALID.RESULTS", "results not an array");
	copy = base;
	copy["results"][0].erase("name");
	expect_refused(parse, copy, "WEATHER.SEARCH_INVALID.NAME", "a place without a name");
	copy = base;
	copy["results"][0]["latitude"] = 123.0;
	expect_refused(parse, copy, "WEATHER.SEARCH_INVALID.PLACE", "latitude out of range");
	copy = base;
	copy["results"][0]["admin1"] = 7;
	expect_refused(parse, copy, "WEATHER.SEARCH_INVALID.PLACE", "region not text");

	const auto located = parse_location(fixture("freeipapi-boulder.json"));
	expect(located.has_value() && located.value().city == "Boulder" && located.value().region == "Colorado",
	       "the recorded location");
	expect(located.has_value() && located.value().latitude == 40.02 && located.value().longitude == -105.27,
	       "location coordinates rounded to 0.01");
	const Json where = Json::parse(fixture("freeipapi-boulder.json"));
	const auto locate = [](const std::string &body) { return parse_location(body); };
	Json changed = where;
	changed.erase("cityName");
	expect_refused(locate, changed, "WEATHER.LOCATION_INVALID.CITY", "no city");
	changed = where;
	changed["latitude"] = 0;
	changed["longitude"] = 0;
	expect_refused(locate, changed, "WEATHER.LOCATION_INVALID.COORDINATES", "0,0 means unknown");
	changed = where;
	changed["longitude"] = "east";
	expect_refused(locate, changed, "WEATHER.LOCATION_INVALID.COORDINATES", "coordinates not numbers");
}

void display_rules()
{
	expect(temperature_text(-0.4, TemperatureUnit::celsius) == "0°C", "-0.4 C is 0, never -0");
	expect(temperature_text(0.5, TemperatureUnit::celsius) == "1°C", "0.5 C rounds up");
	expect(temperature_text(-40.0, TemperatureUnit::fahrenheit) == "-40°F", "-40 is -40 F");
	expect(temperature_text(22.0, TemperatureUnit::fahrenheit) == "72°F", "22 C is 72 F");
	expect(temperature_text(21.85, TemperatureUnit::kelvin) == "295 K", "Kelvin has no degree sign");
	expect(sunrise_text("07:12") == "↑ 7:12" && sunset_text("18:21") == "↓ 18:21", "sun times");
	expect(round_coordinate(40.0149) == 40.01 && round_coordinate(-105.2705) == -105.27, "0.01 degree rounding");
}

void freshness_rules()
{
	DailyForecast forecast;
	forecast.local_date = "2026-11-10";
	forecast.utc_offset_seconds = -25200; // Denver
	const std::int64_t fetched = 1794337200; // 2026-11-10T19:00:00Z, 12:00 in Denver
	forecast.fetched_at = fetched;
	expect(local_date(fetched, -25200) == "2026-11-10", "the place's date at fetch time");
	expect(freshness(forecast, fetched + 60) == Freshness::fresh, "fresh");
	expect(freshness(forecast, fetched + kStaleAfterSeconds) == Freshness::stale, "stale at 3 h");
	expect(freshness(forecast, fetched + 6 * 3600) == Freshness::stale, "still stale before midnight there");
	expect(freshness(forecast, fetched + 12 * 3600) == Freshness::unavailable, "unavailable after the date moves");
	forecast.local_date = "2026-11-09";
	expect(freshness(forecast, fetched + 60) == Freshness::unavailable, "another day's forecast is blank");
	forecast.local_date = "2026-11-10";
	forecast.utc_offset_seconds = 50400; // UTC+14: already the 11th there
	expect(freshness(forecast, fetched) == Freshness::unavailable, "judged in the place's own date");
	forecast.utc_offset_seconds = 0;
	forecast.fetched_at = fetched + 3600;
	expect(freshness(forecast, fetched) == Freshness::unavailable, "a forecast from the future is not shown");
}

void store_encodings()
{
	WeatherSettings settings;
	expect(decode_settings(encode_settings(settings)).has_value() &&
		   decode_settings(encode_settings(settings)).value() == settings,
	       "default settings round-trip");
	settings.unit = TemperatureUnit::kelvin;
	settings.bar = BarContent::sun_times;
	settings.place = Place{"Boulder", "Colorado", "United States", 40.01499, -105.27049};
	const auto decoded = decode_settings(encode_settings(settings));
	expect(decoded.has_value() && decoded.value().place && decoded.value().place->latitude == 40.01 &&
		   decoded.value().place->longitude == -105.27,
	       "a place is stored rounded");
	expect(!decode_settings("{}").has_value() && decode_settings("{}").error().code == "WEATHER.STORE_INVALID",
	       "unknown schema");
	Json both = Json::parse(encode_settings(settings));
	both["location"] = "automatic";
	expect(!decode_settings(both.dump()).has_value(), "a place and Automatic at once");
	DailyForecast forecast;
	forecast.local_date = "2026-11-10";
	forecast.timezone = "America/Denver";
	forecast.utc_offset_seconds = -25200;
	forecast.high_celsius = 20.4;
	forecast.low_celsius = 3.5;
	forecast.sunrise = "06:40";
	forecast.sunset = "16:48";
	forecast.fetched_at = 1794337200;
	forecast.latitude = 40.01;
	forecast.longitude = -105.27;
	const auto again = decode_forecast(encode_forecast(forecast));
	expect(again.has_value() && again.value().high_celsius == 20.4 && again.value().sunset == "16:48" &&
		   again.value().longitude == -105.27,
	       "the forecast round-trips");
	Json corrupt = Json::parse(encode_forecast(forecast));
	corrupt["sunrise"] = "6.40";
	expect(!decode_forecast(corrupt.dump()).has_value(), "a corrupt cached forecast is refused");
}
} // namespace

int main()
{
	forecast_fixture_and_mutations();
	search_and_location();
	display_rules();
	freshness_rules();
	store_encodings();
	std::cout << "weather_panel_codec_test: " << failures << " failures\n";
	return failures ? 1 : 0;
}
