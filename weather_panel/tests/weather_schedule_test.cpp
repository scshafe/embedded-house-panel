#include "weather_panel/weather_schedule.h"

#include <iostream>
#include <string>

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
} // namespace

int main()
{
	WeatherSchedule schedule; // no jitter
	schedule.settings_changed(0, true);
	expect(schedule.location_due(0, true) && schedule.forecast_due(0, false), "both are due at start");
	expect(!schedule.location_due(0, false), "no lookup while a place is chosen");
	schedule.location_succeeded(0);
	schedule.forecast_succeeded(0);
	expect(!schedule.forecast_due(3599, false) && schedule.forecast_due(3600, false), "refresh every 60 min");
	expect(!schedule.location_due(6 * 3600 - 1, true) && schedule.location_due(6 * 3600, true),
	       "the location every 6 h");
	expect(schedule.forecast_due(100, true), "a new local date fetches at once");
	schedule.settings_changed(200, true);
	expect(schedule.forecast_due(200, false), "a settings change fetches at once");
	expect(!schedule.location_due(200, true), "a known location is not looked up again early");

	std::int64_t now = 1000;
	const std::int64_t expected[] = {60, 120, 240, 480, 960, 1800, 1800};
	for (std::int64_t delay : expected) {
		schedule.forecast_failed(now);
		expect(schedule.next_forecast() == now + delay, "backoff " + std::to_string(delay) + " s");
		expect(!schedule.forecast_due(now + 1, true), "a new date does not skip the backoff");
		now = schedule.next_forecast();
	}
	schedule.forecast_succeeded(now);
	expect(schedule.next_forecast() == now + 3600, "a success resets the backoff");
	schedule.forecast_failed(now);
	expect(schedule.next_forecast() == now + 60, "the next failure starts at 1 min again");

	WeatherSchedule jittered([] { return 1.0; });
	jittered.forecast_failed(0);
	expect(jittered.next_forecast() == 48, "jitter shortens a delay by at most a fifth");
	WeatherSchedule looking;
	looking.settings_changed(0, true);
	looking.location_failed(0);
	expect(looking.next_location() == 60 && !looking.location_known(), "a failed lookup backs off");
	std::cout << "weather_panel_schedule_test: " << failures << " failures\n";
	return failures ? 1 : 0;
}
