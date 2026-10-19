#include "bhp/durable_store/durable_store.h"
#include "test_support.h"
using namespace bhp::durable_store;
using namespace std::chrono_literals;
using test::expect;

int main()
{
	expect(!Backoff::create({0ms, 1000ms, 0.2}, [] { return 0.0; }), "zero initial");
	expect(!Backoff::create({2000ms, 1000ms, 0.2}, [] { return 0.0; }), "maximum below initial");
	expect(!Backoff::create({1000ms, 2000ms, 1.5}, [] { return 0.0; }), "jitter above 1");
	expect(!Backoff::create({1000ms, 2000ms, 0.2}, nullptr), "randomness is required");
	auto created = Backoff::create({1000ms, 8000ms, 0.0}, [] { return 0.5; });
	auto backoff = created.value();
	std::vector<long long> delays;
	for (int i = 0; i < 6; ++i)
		delays.push_back(backoff.next_delay().count());
	expect(delays == std::vector<long long>{1000, 2000, 4000, 8000, 8000, 8000}, "doubling up to the maximum");
	expect(backoff.failures() == 6, "failures counted");
	backoff.reset();
	expect(backoff.next_delay().count() == 1000, "reset starts again");
	auto jittered = Backoff::create({1000ms, 8000ms, 0.5}, [] { return 1.0; }).value();
	expect(jittered.next_delay().count() == 500, "full jitter halves a delay at 0.5");
	auto long_outage = Backoff::create({1000ms, 300000ms, 0.0}, [] { return 0.0; }).value();
	for (int i = 0; i < 200; ++i)
		(void)long_outage.next_delay();
	expect(long_outage.next_delay().count() == 300000, "a long outage stays at the maximum without overflow");
	return test::finish("backoff");
}
