#include "weather_panel/weather_store.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

using namespace weather_panel;

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

std::string temporary_directory()
{
	char pattern[] = "/tmp/bhp-weather-store-XXXXXX";
	const char *made = mkdtemp(pattern);
	return made ? std::string(made) + "/state" : std::string();
}

DailyForecast sample()
{
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
	return forecast;
}
} // namespace

int main()
{
	const std::string directory = temporary_directory();
	{
		WeatherStore store(directory);
		const auto defaults = store.settings();
		expect(defaults.has_value() && defaults.value() == WeatherSettings{}, "no file means the ADR 0015 defaults");
		const auto none = store.forecast();
		expect(none.has_value() && !none.value(), "no forecast yet");
		WeatherSettings settings;
		settings.unit = TemperatureUnit::celsius;
		settings.place = Place{"Boulder", "Colorado", "United States", 40.0149, -105.2705};
		expect(store.save_settings(settings).has_value(), "settings saved");
		expect(store.save_forecast(sample()).has_value(), "forecast saved");
	}
	{
		WeatherStore store(directory);
		const auto settings = store.settings();
		expect(settings.has_value() && settings.value().unit == TemperatureUnit::celsius &&
			   settings.value().place && settings.value().place->latitude == 40.01 &&
			   settings.value().place->longitude == -105.27,
		       "settings survive a restart, coordinates rounded");
		const auto forecast = store.forecast();
		expect(forecast.has_value() && forecast.value() && forecast.value()->high_celsius == 20.4,
		       "the forecast survives a restart");
	}
	struct stat info{};
	expect(stat((directory + "/settings.json").c_str(), &info) == 0 && (info.st_mode & 0777) == 0600,
	       "the settings file is 0600");
	expect(stat(directory.c_str(), &info) == 0 && (info.st_mode & 0777) == 0700, "the directory is 0700");
	{
		std::ofstream(directory + "/forecast.json", std::ios::trunc) << "{\"schema\":\"bhp.weather-forecast/v1\"}";
		chmod((directory + "/forecast.json").c_str(), 0600);
		WeatherStore store(directory);
		const auto forecast = store.forecast();
		expect(!forecast.has_value() && forecast.error().code == "WEATHER.STORE_INVALID",
		       "a corrupt cached forecast is refused");
	}
	{
		std::ofstream(directory + "/settings.json", std::ios::trunc) << std::string(20000, ' ');
		chmod((directory + "/settings.json").c_str(), 0600);
		WeatherStore store(directory);
		const auto settings = store.settings();
		expect(!settings.has_value(), "an oversized settings file is refused");
	}
	std::filesystem::remove_all(std::filesystem::path(directory).parent_path());
	std::cout << "weather_panel_store_test: " << failures << " failures\n";
	return failures ? 1 : 0;
}
