#include "bhp/http_runtime/http_runtime.h"
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
using namespace bhp::http_runtime;
int main()
{
	int failures = 0;
	auto check = [&](bool v, const char *s) {
		if (!v) {
			std::cerr << s << '\n';
			++failures;
		}
	};
	check(!validate_budget({0, 100, 100}).has_value(), "zero connect rejected");
	check(!validate_budget({100, 1, 100}).has_value(), "inconsistent deadline rejected");
	check(!validate_budget({1, 1, 0}).has_value(), "zero bytes rejected");
	check(validate_budget({1, 1, 1}).has_value(), "explicit valid budget accepted");
	auto created = HttpRuntime::create();
	check(created.has_value(), "runtime initialized");
	auto runtime = created.value();
	check(!HttpRuntime::create().has_value(), "one process lifetime");
	std::atomic<bool> worker_cancelled{false};
	check(runtime
		  ->start_worker([&](CancellationToken token) {
			  while (!token.cancelled())
				  std::this_thread::yield();
			  worker_cancelled = true;
		  })
		  .has_value(),
	      "worker registered");
	{
		auto handle = runtime->create_handle();
		check(handle.has_value(), "handle after init");
		check(!runtime->shutdown().has_value(), "live handle defers cleanup");
		check(worker_cancelled, "worker joined before deferred cleanup");
		check(!runtime->create_handle().has_value(), "closed runtime refuses handles");
	}
	check(runtime->shutdown().has_value(), "cleanup after handles destroyed");
	check(!runtime->shutdown().has_value(), "repeat shutdown explicit");
	std::cout << "http_runtime: " << (failures ? "failed" : "passed") << '\n';
	return failures ? 1 : 0;
}
