#include "weather_panel/weather_store.h"

#include "bhp/durable_store/durable_store.h"
#include "weather_panel/weather_codec.h"

namespace weather_panel
{
namespace
{
constexpr std::size_t kMaximumBytes = 16 * 1024;

WeatherError store_error(const bhp::durable_store::Error &error)
{
	return {error.code, error.message, false};
}
} // namespace

struct WeatherStore::Implementation {
	bhp::durable_store::DurableFile settings;
	bhp::durable_store::DurableFile forecast;
	explicit Implementation(const std::string &directory)
	    : settings(directory, {"settings.lock", "settings.json", "settings.json.tmp", "WEATHER"}, kMaximumBytes),
	      forecast(directory, {"forecast.lock", "forecast.json", "forecast.json.tmp", "WEATHER"}, kMaximumBytes)
	{
	}
};

WeatherStore::WeatherStore(const std::string &directory) : implementation_(std::make_unique<Implementation>(directory))
{
}

WeatherStore::~WeatherStore() = default;

WeatherResult<WeatherSettings> WeatherStore::settings()
{
	auto read = implementation_->settings.read();
	if (!read)
		return WeatherResult<WeatherSettings>::failure(store_error(read.error()));
	if (!read.value())
		return WeatherResult<WeatherSettings>::success(WeatherSettings{});
	return decode_settings(*read.value());
}

WeatherResult<bool> WeatherStore::save_settings(const WeatherSettings &settings)
{
	auto committed = implementation_->settings.commit(encode_settings(settings));
	if (!committed)
		return WeatherResult<bool>::failure(store_error(committed.error()));
	return WeatherResult<bool>::success(true);
}

WeatherResult<std::optional<DailyForecast>> WeatherStore::forecast()
{
	using Output = WeatherResult<std::optional<DailyForecast>>;
	auto read = implementation_->forecast.read();
	if (!read)
		return Output::failure(store_error(read.error()));
	if (!read.value())
		return Output::success(std::nullopt);
	auto decoded = decode_forecast(*read.value());
	if (!decoded.has_value())
		return Output::failure(decoded.error());
	return Output::success(decoded.value());
}

WeatherResult<bool> WeatherStore::save_forecast(const DailyForecast &forecast)
{
	auto committed = implementation_->forecast.commit(encode_forecast(forecast));
	if (!committed)
		return WeatherResult<bool>::failure(store_error(committed.error()));
	return WeatherResult<bool>::success(true);
}
} // namespace weather_panel
