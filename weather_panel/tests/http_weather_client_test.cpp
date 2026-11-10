#include "weather_panel/weather_client.h"

#include "bhp/structured_logger.h"
#include "fixtures/loopback_http_server.h"

#include <csignal>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace weather_panel;
using Server = vocabulary_panel::test_fixture::LoopbackHttpServer;

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

WeatherEndpoints at(const Server &server, bool trusted = true)
{
	WeatherEndpoints endpoints;
	endpoints.forecast_url = server.base_url() + "/v1/forecast";
	endpoints.search_url = server.base_url() + "/v1/search";
	endpoints.location_url = server.base_url() + "/api/v1/json";
	endpoints.budget = {1000, 1500, 262144};
	if (trusted)
		endpoints.trusted_ca_pem_for_tests = server.trusted_ca_certificate_pem();
	return endpoints;
}
} // namespace

int main()
{
	// The fixture server may write to a connection the client already gave up on.
	std::signal(SIGPIPE, SIG_IGN);
	auto runtime_result = bhp::http_runtime::HttpRuntime::create();
	if (!runtime_result.has_value()) {
		std::cerr << "FAIL: no HTTP runtime\n";
		return 1;
	}
	auto runtime = runtime_result.value();
	std::ostringstream log_text;
	bhp::StructuredLogger logger("http_weather_client_test", log_text);
	bhp::http_runtime::CancellationToken cancel;

	{
		Server server(Server::Transport::https, 200, fixture("open-meteo-forecast-boulder.json"));
		HttpWeatherClient client(runtime, at(server), logger);
		const auto forecast = client.forecast(40.0149, -105.2705, cancel);
		expect(forecast.has_value() && forecast.value().high_celsius == 20.4, "a forecast over HTTPS");
	}
	{
		Server server(Server::Transport::https, 200, fixture("freeipapi-boulder.json"));
		HttpWeatherClient client(runtime, at(server), logger);
		const auto located = client.locate(cancel);
		expect(located.has_value() && located.value().city == "Boulder", "the automatic location");
	}
	{
		Server server(Server::Transport::https, 200, fixture("open-meteo-search-boulder.json"));
		HttpWeatherClient client(runtime, at(server), logger);
		expect(client.search("Boulder", cancel).has_value(), "a search");
		const auto refused = client.search(std::string(61, 'a'), cancel);
		expect(!refused.has_value() && refused.error().code == "WEATHER.SEARCH_NAME_INVALID", "a 61-character search");
	}
	{
		Server server(Server::Transport::https, 200, fixture("open-meteo-forecast-boulder.json"), {},
			      Server::TlsCertificateIdentity::loopback_ip, 2500);
		HttpWeatherClient client(runtime, at(server), logger);
		const auto slow = client.forecast(40.01, -105.27, cancel);
		expect(!slow.has_value() && slow.error().code == "WEATHER.TIMEOUT" && slow.error().retryable, "a timeout");
	}
	for (const long status : {429L, 500L, 404L}) {
		Server server(Server::Transport::https, static_cast<int>(status), "{}");
		HttpWeatherClient client(runtime, at(server), logger);
		const auto answer = client.forecast(40.01, -105.27, cancel);
		expect(!answer.has_value() && answer.error().code == "WEATHER.HTTP_STATUS" &&
			   answer.error().retryable == (status != 404),
		       "HTTP " + std::to_string(status));
	}
	{
		Server server(Server::Transport::https, 200, "{not json");
		HttpWeatherClient client(runtime, at(server), logger);
		const auto answer = client.forecast(40.01, -105.27, cancel);
		expect(!answer.has_value() && answer.error().code == "WEATHER.FORECAST_INVALID.JSON", "malformed JSON");
	}
	{
		Server server(Server::Transport::https, 200, fixture("open-meteo-forecast-boulder.json"));
		auto endpoints = at(server, false);
		HttpWeatherClient client(runtime, endpoints, logger);
		const auto answer = client.forecast(40.01, -105.27, cancel);
		expect(!answer.has_value() && answer.error().code == "WEATHER.TLS_FAILED", "an untrusted certificate");
	}
	{
		WeatherEndpoints plain;
		plain.forecast_url = "http://api.open-meteo.com/v1/forecast";
		bool refused = false;
		try {
			HttpWeatherClient client(runtime, plain, logger);
		} catch (const std::invalid_argument &) {
			refused = true;
		}
		expect(refused, "plain HTTP is refused");
		WeatherEndpoints public_with_test_ca;
		public_with_test_ca.trusted_ca_pem_for_tests = "-----BEGIN CERTIFICATE-----";
		refused = false;
		try {
			HttpWeatherClient client(runtime, public_with_test_ca, logger);
		} catch (const std::invalid_argument &) {
			refused = true;
		}
		expect(refused, "a test CA only with loopback endpoints");
	}
	const std::string logged = log_text.str();
	for (const char *private_text : {"40.01", "-105.27", "40.0149", "192.0.2.1", "Boulder", "latitude"})
		expect(logged.find(private_text) == std::string::npos,
		       std::string("the log never carries ") + private_text);
	expect(logged.find("127.0.0.1") != std::string::npos && logged.find("WEATHER.TIMEOUT") != std::string::npos,
	       "the log carries the host and the code");
	runtime->shutdown();
	std::cout << "weather_panel_http_client_test: " << failures << " failures\n";
	return failures ? 1 : 0;
}
