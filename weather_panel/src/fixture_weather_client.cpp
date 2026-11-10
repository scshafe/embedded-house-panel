#include "weather_panel/weather_client.h"
#include "weather_panel/weather_codec.h"

#include <cctype>
#include <fstream>
#include <sstream>

namespace weather_panel
{
namespace
{
WeatherResult<std::string> read_fixture(const std::string &path)
{
	std::ifstream input(path, std::ios::binary);
	if (!input)
		return WeatherResult<std::string>::failure(
		    {"WEATHER.NETWORK_UNAVAILABLE", "The fixture answer is missing", true});
	std::ostringstream text;
	text << input.rdbuf();
	return WeatherResult<std::string>::success(text.str());
}
} // namespace

bool search_name_valid(const std::string &name)
{
	if (name.empty() || name.size() > 240)
		return false;
	std::size_t characters = 0;
	for (unsigned char byte : name) {
		if (byte < 0x20 || byte == 0x7f)
			return false;
		if ((byte & 0xc0) != 0x80)
			++characters;
	}
	return characters >= 1 && characters <= 60;
}

FixtureWeatherClient::FixtureWeatherClient(std::string directory) : directory_(std::move(directory)) {}

WeatherResult<DailyForecast> FixtureWeatherClient::forecast(double, double, bhp::http_runtime::CancellationToken)
{
	auto body = read_fixture(directory_ + "/open-meteo-forecast-boulder.json");
	if (!body.has_value())
		return WeatherResult<DailyForecast>::failure(body.error());
	return parse_forecast(body.value());
}

WeatherResult<std::vector<Place>> FixtureWeatherClient::search(const std::string &name,
								bhp::http_runtime::CancellationToken)
{
	if (!search_name_valid(name))
		return WeatherResult<std::vector<Place>>::failure(
		    {"WEATHER.SEARCH_NAME_INVALID", "A search needs 1-60 characters", false});
	// "Boulder" (any case) is the recorded match; anything else is the recorded no-match answer.
	std::string lowered;
	for (char character : name)
		lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
	auto body = read_fixture(directory_ + (lowered.rfind("boul", 0) == 0 ? "/open-meteo-search-boulder.json"
									     : "/open-meteo-search-none.json"));
	if (!body.has_value())
		return WeatherResult<std::vector<Place>>::failure(body.error());
	return parse_search(body.value());
}

WeatherResult<ApproximateLocation> FixtureWeatherClient::locate(bhp::http_runtime::CancellationToken)
{
	auto body = read_fixture(directory_ + "/freeipapi-boulder.json");
	if (!body.has_value())
		return WeatherResult<ApproximateLocation>::failure(body.error());
	return parse_location(body.value());
}
} // namespace weather_panel
