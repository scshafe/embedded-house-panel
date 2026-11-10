#include "weather_panel/weather_worker.h"

#include "bhp/structured_logger.h"
#include "weather_panel/weather_codec.h"
#include "weather_panel/weather_schedule.h"

#include <condition_variable>
#include <mutex>
#include <random>
#include <thread>

namespace weather_panel
{
namespace
{
std::optional<std::pair<double, double>> target_of(const WeatherSettings &settings,
						   const std::optional<ApproximateLocation> &location)
{
	if (settings.place)
		return std::make_pair(settings.place->latitude, settings.place->longitude);
	if (location)
		return std::make_pair(location->latitude, location->longitude);
	return std::nullopt;
}

bool for_target(const DailyForecast &forecast, const std::pair<double, double> &target)
{
	return round_coordinate(forecast.latitude) == round_coordinate(target.first) &&
	       round_coordinate(forecast.longitude) == round_coordinate(target.second);
}
} // namespace

WeatherClocks system_weather_clocks()
{
	return {[] {
			return static_cast<std::int64_t>(std::chrono::duration_cast<std::chrono::seconds>(
							     std::chrono::steady_clock::now().time_since_epoch())
							     .count());
		},
		[] {
			return static_cast<std::int64_t>(std::chrono::duration_cast<std::chrono::seconds>(
							     std::chrono::system_clock::now().time_since_epoch())
							     .count());
		}};
}

struct WeatherWorker::State {
	std::shared_ptr<WeatherClient> client;
	std::shared_ptr<WeatherStore> store;
	WeatherClocks clocks;
	bhp::StructuredLogger *logger{nullptr};
	std::chrono::milliseconds tick;
	mutable std::mutex mutex;
	std::condition_variable changed;
	WeatherSchedule schedule;
	WeatherSnapshot snapshot;
	std::optional<DailyForecast> cached;
	std::optional<std::pair<std::uint64_t, std::string>> pending_search;
	std::optional<SearchOutcome> search_outcome;
	std::uint64_t search_requests{0};
	bhp::http_runtime::CancellationToken cancellation;
	bool closing{false};
	std::thread thread;

	State(std::shared_ptr<WeatherClient> c, std::shared_ptr<WeatherStore> s, WeatherClocks k, bhp::StructuredLogger *l,
	      std::chrono::milliseconds t)
	    : client(std::move(c)), store(std::move(s)), clocks(std::move(k)), logger(l), tick(t),
	      schedule([generator = std::make_shared<std::mt19937>(std::random_device{}())] {
		      return std::uniform_real_distribution<double>(0.0, 1.0)(*generator);
	      })
	{
	}

	void log(const char *message, const WeatherError &error)
	{
		if (!logger)
			return;
		try {
			logger->log(bhp::StructuredLogLevel::warn, message, "weather", {{"error_code", error.code}});
		} catch (const std::exception &) {
		}
	}

	/** Under the mutex: what the UI sees, with a forecast only for the selected place. */
	void publish()
	{
		const auto target = target_of(snapshot.settings, snapshot.location);
		snapshot.forecast = cached && target && for_target(*cached, *target) ? cached : std::nullopt;
		++snapshot.revision;
	}

	void run()
	{
		for (;;) {
			std::unique_lock<std::mutex> lock(mutex);
			changed.wait_for(lock, tick, [&] { return closing || pending_search.has_value(); });
			if (closing)
				return;
			if (pending_search) {
				const auto [request, name] = *pending_search;
				pending_search.reset();
				lock.unlock();
				auto answer = client->search(name, cancellation);
				lock.lock();
				if (closing)
					return;
				if (request == search_requests) {
					SearchOutcome outcome{request, {}, std::nullopt};
					if (answer.has_value())
						outcome.places = answer.value();
					else {
						outcome.error = answer.error();
						log("weather_search_failed", answer.error());
					}
					search_outcome = std::move(outcome);
				}
			}
			const std::int64_t now = clocks.monotonic_seconds();
			const bool automatic = !snapshot.settings.place;
			if (schedule.location_due(now, automatic)) {
				lock.unlock();
				auto located = client->locate(cancellation);
				lock.lock();
				if (closing)
					return;
				if (located.has_value()) {
					const bool moved = !snapshot.location ||
							   snapshot.location->latitude != located.value().latitude ||
							   snapshot.location->longitude != located.value().longitude;
					snapshot.location = located.value();
					snapshot.location_error.reset();
					schedule.location_succeeded(now);
					if (moved)
						schedule.settings_changed(now, false);
				} else {
					snapshot.location_error = located.error();
					schedule.location_failed(now);
					log("weather_location_failed", located.error());
				}
				publish();
			}
			const auto target = target_of(snapshot.settings, snapshot.location);
			if (!target)
				continue;
			const std::int64_t utc = clocks.utc_seconds();
			const bool date_moved = cached && for_target(*cached, *target) &&
						local_date(utc, cached->utc_offset_seconds) != cached->local_date;
			if (!schedule.forecast_due(now, date_moved))
				continue;
			lock.unlock();
			auto fetched = client->forecast(target->first, target->second, cancellation);
			lock.lock();
			if (closing)
				return;
			if (fetched.has_value()) {
				DailyForecast forecast = fetched.value();
				forecast.fetched_at = utc;
				forecast.latitude = round_coordinate(target->first);
				forecast.longitude = round_coordinate(target->second);
				cached = forecast;
				snapshot.forecast_error.reset();
				schedule.forecast_succeeded(now);
				if (store) {
					auto saved = store->save_forecast(forecast);
					if (!saved.has_value())
						log("weather_store_failed", saved.error());
				}
			} else {
				snapshot.forecast_error = fetched.error();
				schedule.forecast_failed(now);
				log("weather_forecast_failed", fetched.error());
			}
			publish();
		}
	}
};

WeatherWorker::WeatherWorker(std::shared_ptr<WeatherClient> client, std::shared_ptr<WeatherStore> store,
			     WeatherClocks clocks, bhp::StructuredLogger *logger, std::chrono::milliseconds tick)
    : state_(std::make_shared<State>(std::move(client), std::move(store), std::move(clocks), logger, tick))
{
	if (!state_->client || !state_->clocks.monotonic_seconds || !state_->clocks.utc_seconds)
		throw std::invalid_argument("WEATHER.CONFIG_INVALID");
	if (state_->store) {
		auto settings = state_->store->settings();
		if (settings.has_value())
			state_->snapshot.settings = settings.value();
		else
			state_->log("weather_store_failed", settings.error());
		auto forecast = state_->store->forecast();
		if (forecast.has_value())
			state_->cached = forecast.value();
		else
			state_->log("weather_store_failed", forecast.error());
	}
	state_->schedule.settings_changed(state_->clocks.monotonic_seconds(), !state_->snapshot.settings.place);
	state_->publish();
	state_->thread = std::thread([state = state_] { state->run(); });
}

WeatherWorker::~WeatherWorker()
{
	{
		std::lock_guard<std::mutex> lock(state_->mutex);
		state_->closing = true;
		state_->cancellation.cancel();
	}
	state_->changed.notify_all();
	if (state_->thread.joinable())
		state_->thread.join();
}

WeatherSnapshot WeatherWorker::snapshot() const
{
	std::lock_guard<std::mutex> lock(state_->mutex);
	return state_->snapshot;
}

void WeatherWorker::update_settings(const WeatherSettings &settings)
{
	std::lock_guard<std::mutex> lock(state_->mutex);
	WeatherSettings rounded = settings;
	if (rounded.place) {
		rounded.place->latitude = round_coordinate(rounded.place->latitude);
		rounded.place->longitude = round_coordinate(rounded.place->longitude);
	}
	state_->snapshot.settings = rounded;
	if (state_->store) {
		auto saved = state_->store->save_settings(rounded);
		if (!saved.has_value())
			state_->log("weather_store_failed", saved.error());
	}
	state_->schedule.settings_changed(state_->clocks.monotonic_seconds(), !rounded.place);
	state_->publish();
	state_->changed.notify_all();
}

std::uint64_t WeatherWorker::search(const std::string &name)
{
	std::lock_guard<std::mutex> lock(state_->mutex);
	state_->pending_search = std::make_pair(++state_->search_requests, name);
	state_->search_outcome.reset();
	state_->changed.notify_all();
	return state_->search_requests;
}

std::optional<SearchOutcome> WeatherWorker::take_search()
{
	std::lock_guard<std::mutex> lock(state_->mutex);
	auto outcome = std::move(state_->search_outcome);
	state_->search_outcome.reset();
	return outcome;
}
} // namespace weather_panel
