#pragma once

#include "weather_panel/weather_models.h"

#include <memory>
#include <optional>
#include <string>

namespace weather_panel
{
/**
 * @canonical weather_store -- the settings and the last good forecast, on the UI's own storage.
 *
 * Two owner-only files (0600, in a 0700 directory), each written atomically by
 * bhp::durable_store::DurableFile and bounded to 16 KiB. Coordinates are written rounded.
 * A file that is oversized, corrupt or of an unknown schema is WEATHER.STORE_INVALID.
 */
class WeatherStore final
{
      public:
	explicit WeatherStore(const std::string &directory);
	~WeatherStore();
	WeatherStore(const WeatherStore &) = delete;
	WeatherStore &operator=(const WeatherStore &) = delete;
	/** The saved settings, or the defaults when none were saved. */
	WeatherResult<WeatherSettings> settings();
	WeatherResult<bool> save_settings(const WeatherSettings &settings);
	/** The last good forecast, or none. */
	WeatherResult<std::optional<DailyForecast>> forecast();
	WeatherResult<bool> save_forecast(const DailyForecast &forecast);

      private:
	struct Implementation;
	std::unique_ptr<Implementation> implementation_;
};
} // namespace weather_panel
