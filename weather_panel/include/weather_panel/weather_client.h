#pragma once

#include "bhp/http_runtime/http_runtime.h"
#include "weather_panel/weather_models.h"

#include <memory>
#include <string>
#include <vector>

namespace bhp
{
class StructuredLogger;
}

namespace weather_panel
{
/** @extension-point weather_client -- the three pinned sources; fixture and live implementations. */
class WeatherClient
{
      public:
	virtual ~WeatherClient() = default;
	/** Today's forecast at rounded coordinates; `fetched_at` is the caller's to set. */
	virtual WeatherResult<DailyForecast> forecast(double latitude, double longitude,
						      bhp::http_runtime::CancellationToken cancel) = 0;
	/** Up to five places for a name of 1-60 characters. */
	virtual WeatherResult<std::vector<Place>> search(const std::string &name,
							 bhp::http_runtime::CancellationToken cancel) = 0;
	/** The approximate location of the panel's public address (sends that address to the service). */
	virtual WeatherResult<ApproximateLocation> locate(bhp::http_runtime::CancellationToken cancel) = 0;
};

/** The pinned endpoints (ADR 0015). Tests point them at a loopback HTTPS fixture. */
struct WeatherEndpoints {
	std::string forecast_url{"https://api.open-meteo.com/v1/forecast"};
	std::string search_url{"https://geocoding-api.open-meteo.com/v1/search"};
	std::string location_url{"https://free.freeipapi.com/api/v1/json"};
	bhp::http_runtime::TransportBudget budget{5000, 15000, 262144};
	/** Test-only trust anchor; accepted only with https://127.0.0.1 endpoints. */
	std::string trusted_ca_pem_for_tests;
};

/** Search names: 1-60 characters, no control characters. */
bool search_name_valid(const std::string &name);

/**
 * @canonical http_weather_client -- one HTTPS GET per call on shared/http_runtime.
 *
 * Its log lines carry the host and a code, never coordinates, a place name or an address.
 * Transport failures are WEATHER.TIMEOUT, WEATHER.TLS_FAILED or WEATHER.NETWORK_UNAVAILABLE;
 * a status other than 200 is WEATHER.HTTP_STATUS (retryable for 429 and 5xx).
 */
class HttpWeatherClient final : public WeatherClient
{
      public:
	/** Throws std::invalid_argument (WEATHER.CONFIG_INVALID) for a non-HTTPS endpoint or a misplaced test CA. */
	HttpWeatherClient(std::shared_ptr<bhp::http_runtime::HttpRuntime> runtime, WeatherEndpoints endpoints,
			  bhp::StructuredLogger &logger);
	WeatherResult<DailyForecast> forecast(double latitude, double longitude,
					      bhp::http_runtime::CancellationToken cancel) override;
	WeatherResult<std::vector<Place>> search(const std::string &name,
						 bhp::http_runtime::CancellationToken cancel) override;
	WeatherResult<ApproximateLocation> locate(bhp::http_runtime::CancellationToken cancel) override;

      private:
	WeatherResult<std::string> get(const std::string &url, bhp::http_runtime::CancellationToken cancel);
	std::shared_ptr<bhp::http_runtime::HttpRuntime> runtime_;
	WeatherEndpoints endpoints_;
	bhp::StructuredLogger &logger_;
};

/**
 * The simulator's and the VM guest's source: the recorded answers in a fixture directory
 * (weather_panel/tests/fixtures). It never touches the network.
 */
class FixtureWeatherClient final : public WeatherClient
{
      public:
	explicit FixtureWeatherClient(std::string directory);
	WeatherResult<DailyForecast> forecast(double latitude, double longitude,
					      bhp::http_runtime::CancellationToken cancel) override;
	WeatherResult<std::vector<Place>> search(const std::string &name,
						 bhp::http_runtime::CancellationToken cancel) override;
	WeatherResult<ApproximateLocation> locate(bhp::http_runtime::CancellationToken cancel) override;

      private:
	std::string directory_;
};
} // namespace weather_panel
