#include "weather_panel/weather_codec.h"
#include "weather_panel/weather_worker.h"

#include "bhp/structured_logger.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>
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

std::string fixture(const std::string &name)
{
	std::ifstream input(std::string(WEATHER_FIXTURE_DIR) + "/" + name);
	std::ostringstream text;
	text << input.rdbuf();
	return text.str();
}

/** Recorded answers, counted; any call can be made to fail. */
class ScriptedClient final : public WeatherClient
{
      public:
	std::atomic<int> forecasts{0}, searches{0}, lookups{0};
	std::atomic<bool> forecast_fails{false}, lookup_fails{false};
	std::mutex mutex;
	std::vector<std::pair<double, double>> asked;

	WeatherResult<DailyForecast> forecast(double latitude, double longitude,
					      bhp::http_runtime::CancellationToken) override
	{
		++forecasts;
		{
			std::lock_guard<std::mutex> lock(mutex);
			asked.emplace_back(latitude, longitude);
		}
		if (forecast_fails)
			return WeatherResult<DailyForecast>::failure({"WEATHER.NETWORK_UNAVAILABLE", "down", true});
		return parse_forecast(fixture("open-meteo-forecast-boulder.json"));
	}
	WeatherResult<std::vector<Place>> search(const std::string &name, bhp::http_runtime::CancellationToken) override
	{
		++searches;
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
		return parse_search(fixture(name == "Boulder" ? "open-meteo-search-boulder.json" : "open-meteo-search-none.json"));
	}
	WeatherResult<ApproximateLocation> locate(bhp::http_runtime::CancellationToken) override
	{
		++lookups;
		if (lookup_fails)
			return WeatherResult<ApproximateLocation>::failure({"WEATHER.TIMEOUT", "slow", true});
		return parse_location(fixture("freeipapi-boulder.json"));
	}
};

struct FakeClocks {
	std::atomic<std::int64_t> monotonic{1000};
	std::atomic<std::int64_t> utc{1794337200}; // 2026-11-10T19:00:00Z
	WeatherClocks clocks()
	{
		return {[this] { return monotonic.load(); }, [this] { return utc.load(); }};
	}
};

template <typename Condition> bool eventually(Condition condition)
{
	for (int i = 0; i < 400; ++i) {
		if (condition())
			return true;
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
	return condition();
}

std::string temporary_directory()
{
	char pattern[] = "/tmp/bhp-weather-worker-XXXXXX";
	const char *made = mkdtemp(pattern);
	return made ? std::string(made) + "/state" : std::string();
}

void automatic_then_a_place(const std::string &directory)
{
	auto client = std::make_shared<ScriptedClient>();
	FakeClocks time;
	std::ostringstream log_text;
	bhp::StructuredLogger logger("weather_worker_test", log_text);
	{
		WeatherWorker worker(client, std::make_shared<WeatherStore>(directory), time.clocks(), &logger,
				     std::chrono::milliseconds(5));
		expect(eventually([&] { return worker.snapshot().forecast.has_value(); }),
		       "Automatic: the location, then the forecast");
		auto snapshot = worker.snapshot();
		expect(snapshot.location && snapshot.location->city == "Boulder", "the automatic location is shown");
		expect(client->lookups == 1 && client->forecasts == 1, "one lookup and one forecast at start");
		{
			std::lock_guard<std::mutex> lock(client->mutex);
			expect(!client->asked.empty() && client->asked[0].first == 40.02 && client->asked[0].second == -105.27,
			       "the forecast asks for rounded coordinates");
		}
		time.monotonic += 3599;
		std::this_thread::sleep_for(std::chrono::milliseconds(40));
		expect(client->forecasts == 1, "no refresh before 60 min");
		time.monotonic += 1;
		expect(eventually([&] { return client->forecasts == 2; }), "a refresh at 60 min");

		WeatherSettings chosen;
		chosen.place = Place{"Paris", "Ile-de-France", "France", 48.8566, 2.3522};
		worker.update_settings(chosen);
		snapshot = worker.snapshot();
		expect(!snapshot.forecast, "Boulder's forecast is never shown for Paris");
		expect(eventually([&] { return worker.snapshot().forecast.has_value(); }), "Paris's forecast arrives");
		{
			std::lock_guard<std::mutex> lock(client->mutex);
			expect(client->asked.back().first == 48.86 && client->asked.back().second == 2.35,
			       "the chosen place is asked for rounded");
		}
		time.monotonic += 7 * 3600;
		std::this_thread::sleep_for(std::chrono::milliseconds(40));
		expect(client->lookups == 1, "no lookup while a place is chosen");

		const auto first = worker.search("Nowhere");
		const auto second = worker.search("Boulder");
		expect(second == first + 1, "search requests are numbered");
		expect(eventually([&] {
			       auto outcome = worker.take_search();
			       if (!outcome)
				       return false;
			       expect(outcome->request == second && outcome->places.size() == 5, "the newest search wins");
			       return true;
		       }),
		       "a search answers");

		client->forecast_fails = true;
		worker.update_settings(chosen);
		expect(eventually([&] { return worker.snapshot().forecast_error.has_value(); }), "a failure is reported");
		const int after_failure = client->forecasts;
		time.monotonic += 47; // the first retry waits 1 min, shortened by at most a fifth
		std::this_thread::sleep_for(std::chrono::milliseconds(40));
		expect(client->forecasts == after_failure, "the backoff waits at least 48 s");
		time.monotonic += 13;
		expect(eventually([&] { return client->forecasts == after_failure + 1; }), "then it tries again");
		expect(worker.snapshot().forecast.has_value(), "the last good forecast stays during failures");
	}
	const std::string logged = log_text.str();
	for (const char *private_text : {"40.02", "-105.27", "48.86", "2.35", "192.0.2.1", "Boulder", "Paris"})
		expect(logged.find(private_text) == std::string::npos,
		       std::string("the log never carries ") + private_text);
	expect(logged.find("WEATHER.NETWORK_UNAVAILABLE") != std::string::npos, "the log carries the code");
}

void restart_keeps_settings_and_forecast(const std::string &directory)
{
	auto client = std::make_shared<ScriptedClient>();
	client->forecast_fails = true;
	FakeClocks time;
	WeatherWorker worker(client, std::make_shared<WeatherStore>(directory), time.clocks(), nullptr,
			     std::chrono::milliseconds(5));
	const auto snapshot = worker.snapshot();
	expect(snapshot.settings.place && snapshot.settings.place->name == "Paris", "the chosen place survives a restart");
	expect(snapshot.forecast.has_value(), "the cached forecast for it is shown at once");
}

void automatic_unavailable()
{
	auto client = std::make_shared<ScriptedClient>();
	client->lookup_fails = true;
	FakeClocks time;
	WeatherWorker worker(client, nullptr, time.clocks(), nullptr, std::chrono::milliseconds(5));
	expect(eventually([&] { return worker.snapshot().location_error.has_value(); }), "Automatic reports unavailable");
	std::this_thread::sleep_for(std::chrono::milliseconds(40));
	expect(client->forecasts == 0 && !worker.snapshot().forecast, "no forecast without a location");
	expect(client->lookups == 1, "the lookup backs off");
}
} // namespace

int main()
{
	const std::string directory = temporary_directory();
	automatic_then_a_place(directory);
	restart_keeps_settings_and_forecast(directory);
	automatic_unavailable();
	std::filesystem::remove_all(std::filesystem::path(directory).parent_path());
	std::cout << "weather_panel_worker_test: " << failures << " failures\n";
	return failures ? 1 : 0;
}
