#include "bhp/http_runtime/http_request.h"
#include "bhp/structured_logger.h"
#include "weather_panel/weather_client.h"
#include "weather_panel/weather_codec.h"

#include <cctype>
#include <cstdio>
#include <regex>
#include <stdexcept>

namespace weather_panel
{
namespace
{
const std::regex kSecure(R"(https://([A-Za-z0-9.-]+)(:[0-9]{1,5})?(/[A-Za-z0-9/._-]*)?)");
const std::regex kLoopbackSecure(R"(https://127\.0\.0\.1:[0-9]{1,5}(/[A-Za-z0-9/._-]*)?)");

std::string host_of(const std::string &url)
{
	std::smatch match;
	return std::regex_match(url, match, kSecure) ? match[1].str() : std::string("unknown");
}

std::string coordinate(double degrees)
{
	char text[24];
	std::snprintf(text, sizeof(text), "%.2f", round_coordinate(degrees));
	return text;
}

std::string percent_encoded(const std::string &text)
{
	static const char *hex = "0123456789ABCDEF";
	std::string out;
	for (unsigned char byte : text) {
		if (std::isalnum(byte) || byte == '-' || byte == '_' || byte == '.' || byte == '~') {
			out.push_back(static_cast<char>(byte));
		} else {
			out.push_back('%');
			out.push_back(hex[byte >> 4]);
			out.push_back(hex[byte & 15]);
		}
	}
	return out;
}

WeatherError transport_error(const bhp::http_runtime::Error &error)
{
	if (error.code == "HTTP.CANCELLED")
		return {"WEATHER.CANCELLED", "The weather request was cancelled", false};
	if (error.code == "HTTP.TIMEOUT")
		return {"WEATHER.TIMEOUT", "The weather service did not answer in time", true};
	if (error.code == "HTTP.TLS_VERIFICATION_FAILED")
		return {"WEATHER.TLS_FAILED", "The weather service's certificate was not trusted", true};
	if (error.code == "HTTP.RESPONSE_TOO_LARGE")
		return {"WEATHER.RESPONSE_TOO_LARGE", "The weather answer was too large", false};
	return {"WEATHER.NETWORK_UNAVAILABLE", "The weather service could not be reached", true};
}
} // namespace

HttpWeatherClient::HttpWeatherClient(std::shared_ptr<bhp::http_runtime::HttpRuntime> runtime,
				     WeatherEndpoints endpoints, bhp::StructuredLogger &logger)
    : runtime_(std::move(runtime)), endpoints_(std::move(endpoints)), logger_(logger)
{
	if (!runtime_ || !bhp::http_runtime::validate_budget(endpoints_.budget).has_value())
		throw std::invalid_argument("WEATHER.CONFIG_INVALID");
	for (const auto *url : {&endpoints_.forecast_url, &endpoints_.search_url, &endpoints_.location_url}) {
		if (!std::regex_match(*url, kSecure))
			throw std::invalid_argument("WEATHER.CONFIG_INVALID");
		if (!endpoints_.trusted_ca_pem_for_tests.empty() && !std::regex_match(*url, kLoopbackSecure))
			throw std::invalid_argument("WEATHER.CONFIG_INVALID");
	}
}

WeatherResult<std::string> HttpWeatherClient::get(const std::string &url, bhp::http_runtime::CancellationToken cancel)
{
	using Output = WeatherResult<std::string>;
	bhp::http_runtime::HttpRequest request;
	request.method = bhp::http_runtime::HttpMethod::Get;
	request.url = url;
	request.headers = {{"Accept", "application/json"}};
	request.budget = endpoints_.budget;
	request.trusted_ca_pem_for_tests = endpoints_.trusted_ca_pem_for_tests;
	auto exchange = bhp::http_runtime::perform(
	    *runtime_, request, bhp::http_runtime::CancellationToken::linked(cancel, runtime_->cancellation()));
	// The host only: a URL here carries coordinates or a place name, which are never logged.
	const std::string host = host_of(url);
	if (!exchange.has_value()) {
		const auto error = transport_error(exchange.error());
		logger_.log(bhp::StructuredLogLevel::warn, "weather_request_failed", "weather",
			    {{"host", host}, {"error_code", error.code}});
		return Output::failure(error);
	}
	const long status = exchange.value().status;
	if (status != 200) {
		logger_.log(bhp::StructuredLogLevel::warn, "weather_request_failed", "weather",
			    {{"host", host}, {"error_code", "WEATHER.HTTP_STATUS"}, {"http_status", std::to_string(status)}});
		return Output::failure({"WEATHER.HTTP_STATUS", "The weather service answered " + std::to_string(status),
					status == 429 || status >= 500});
	}
	logger_.log(bhp::StructuredLogLevel::info, "weather_request_answered", "weather", {{"host", host}});
	return Output::success(exchange.value().body);
}

WeatherResult<DailyForecast> HttpWeatherClient::forecast(double latitude, double longitude,
							  bhp::http_runtime::CancellationToken cancel)
{
	auto body = get(endpoints_.forecast_url + "?latitude=" + coordinate(latitude) + "&longitude=" +
			    coordinate(longitude) +
			    "&daily=temperature_2m_max,temperature_2m_min,sunrise,sunset&timezone=auto&forecast_days=1",
			cancel);
	if (!body.has_value())
		return WeatherResult<DailyForecast>::failure(body.error());
	return parse_forecast(body.value());
}

WeatherResult<std::vector<Place>> HttpWeatherClient::search(const std::string &name,
							     bhp::http_runtime::CancellationToken cancel)
{
	if (!search_name_valid(name))
		return WeatherResult<std::vector<Place>>::failure(
		    {"WEATHER.SEARCH_NAME_INVALID", "A search needs 1-60 characters", false});
	auto body = get(endpoints_.search_url + "?name=" + percent_encoded(name) + "&count=5&language=en", cancel);
	if (!body.has_value())
		return WeatherResult<std::vector<Place>>::failure(body.error());
	return parse_search(body.value());
}

WeatherResult<ApproximateLocation> HttpWeatherClient::locate(bhp::http_runtime::CancellationToken cancel)
{
	auto body = get(endpoints_.location_url, cancel);
	if (!body.has_value())
		return WeatherResult<ApproximateLocation>::failure(body.error());
	return parse_location(body.value());
}
} // namespace weather_panel
