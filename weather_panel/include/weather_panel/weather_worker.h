#pragma once

#include "weather_panel/weather_client.h"
#include "weather_panel/weather_models.h"
#include "weather_panel/weather_store.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace bhp
{
class StructuredLogger;
}

namespace weather_panel
{
struct WeatherClocks {
	/** Monotonic seconds, for the schedule. */
	std::function<std::int64_t()> monotonic_seconds;
	/** Unix seconds UTC, for dates and freshness. */
	std::function<std::int64_t()> utc_seconds;
};
WeatherClocks system_weather_clocks();

/** What the bar and the menu draw from, read on the UI thread. */
struct WeatherSnapshot {
	WeatherSettings settings;
	/** The last good forecast for the selected place only; never one fetched for another. */
	std::optional<DailyForecast> forecast;
	/** The automatic location, while Automatic is selected and one has been found. */
	std::optional<ApproximateLocation> location;
	std::optional<WeatherError> forecast_error;
	std::optional<WeatherError> location_error;
	/** Grows with every change, so the UI redraws only when it moved. */
	std::uint64_t revision{0};
};

struct SearchOutcome {
	std::uint64_t request{0};
	std::vector<Place> places;
	std::optional<WeatherError> error;
};

/**
 * @canonical weather_worker -- the weather's own thread, off the LVGL thread.
 *
 * It follows WeatherSchedule, keeps the settings and the last good forecast in the store,
 * and answers one search at a time (the newest wins). The UI thread only reads snapshot()
 * and take_search(), and calls update_settings() and search(); none of them blocks on the
 * network. Its log lines carry codes, never coordinates, names or addresses.
 */
class WeatherWorker final
{
      public:
	WeatherWorker(std::shared_ptr<WeatherClient> client, std::shared_ptr<WeatherStore> store, WeatherClocks clocks,
		      bhp::StructuredLogger *logger, std::chrono::milliseconds tick = std::chrono::milliseconds(250));
	~WeatherWorker();
	WeatherWorker(const WeatherWorker &) = delete;
	WeatherWorker &operator=(const WeatherWorker &) = delete;
	WeatherSnapshot snapshot() const;
	/** Saves them and refreshes at once. */
	void update_settings(const WeatherSettings &settings);
	/** Starts a search; returns its request number. A newer search replaces an unanswered one. */
	std::uint64_t search(const std::string &name);
	std::optional<SearchOutcome> take_search();

      private:
	struct State;
	std::shared_ptr<State> state_;
};
} // namespace weather_panel
