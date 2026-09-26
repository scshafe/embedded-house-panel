#include "bhp/http_runtime/http_runtime.h"
#include <curl/curl.h>
#include <limits>
#include <mutex>
#include <thread>
#include <vector>
namespace bhp::http_runtime
{
namespace
{
std::mutex process_mutex;
bool process_live = false;
} // namespace
struct HttpRuntime::State {
	std::mutex mutex;
	CancellationToken cancellation;
	std::vector<std::thread> workers;
	std::size_t handles{0};
	bool closing{false}, closed{false}, shutdown_running{false};
	~State()
	{ // Last handle releases the global lifetime even on an incorrectly ordered client
	  // teardown.
		if (!closed) {
			std::lock_guard<std::mutex> process_lock(process_mutex);
			curl_global_cleanup();
			process_live = false;
		}
	}
};
Result<TransportBudget> validate_budget(TransportBudget b)
{
	if (b.connect_timeout_ms <= 0 || b.request_timeout_ms <= 0 ||
	    b.connect_timeout_ms > b.request_timeout_ms || b.maximum_response_bytes == 0 ||
	    b.maximum_response_bytes > static_cast<std::size_t>(std::numeric_limits<int>::max()))
		return Result<TransportBudget>::failure(
		    {"HTTP_RUNTIME.BUDGET_INVALID",
		     "Connect/request deadlines and response ceiling must be positive and "
		     "internally consistent"});
	return Result<TransportBudget>::success(b);
}
Result<std::shared_ptr<HttpRuntime>> HttpRuntime::create()
{
	std::lock_guard<std::mutex> lock(process_mutex);
	if (process_live)
		return Result<std::shared_ptr<HttpRuntime>>::failure(
		    {"HTTP_RUNTIME.ALREADY_INITIALIZED",
		     "One live process HTTP runtime is permitted"});
	if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
		return Result<std::shared_ptr<HttpRuntime>>::failure(
		    {"HTTP_RUNTIME.INIT_FAILED", "libcurl global initialization failed"});
	process_live = true;
	return Result<std::shared_ptr<HttpRuntime>>::success(
	    std::shared_ptr<HttpRuntime>(new HttpRuntime(std::make_shared<State>())));
}
HttpRuntime::~HttpRuntime()
{
	if (!state_->closed)
		static_cast<void>(shutdown());
}
HttpRuntime::Handle::~Handle()
{
	curl_easy_cleanup(static_cast<CURL *>(handle_));
	std::lock_guard<std::mutex> lock(state_->mutex);
	--state_->handles;
}
Result<std::shared_ptr<HttpRuntime::Handle>> HttpRuntime::create_handle()
{
	std::lock_guard<std::mutex> lock(state_->mutex);
	if (state_->closing || state_->closed)
		return Result<std::shared_ptr<Handle>>::failure(
		    {"HTTP_RUNTIME.CLOSED", "Cannot create an HTTP handle during shutdown"});
	auto *handle = curl_easy_init();
	if (!handle)
		return Result<std::shared_ptr<Handle>>::failure(
		    {"HTTP_RUNTIME.HANDLE_FAILED", "Failed to allocate libcurl easy handle"});
	++state_->handles;
	return Result<std::shared_ptr<Handle>>::success(
	    std::shared_ptr<Handle>(new Handle(state_, handle)));
}
Result<bool> HttpRuntime::start_worker(std::function<void(CancellationToken)> work)
{
	std::lock_guard<std::mutex> lock(state_->mutex);
	if (state_->closing || state_->closed)
		return Result<bool>::failure(
		    {"HTTP_RUNTIME.CLOSED", "Cannot register HTTP work during shutdown"});
	const auto cancel = state_->cancellation;
	state_->workers.emplace_back([work = std::move(work), cancel] { work(cancel); });
	return Result<bool>::success(true);
}
CancellationToken HttpRuntime::cancellation() const { return state_->cancellation; }
Result<bool> HttpRuntime::shutdown()
{
	std::vector<std::thread> workers;
	{
		std::lock_guard<std::mutex> lock(state_->mutex);
		if (state_->closed)
			return Result<bool>::failure({"HTTP_RUNTIME.ALREADY_SHUT_DOWN",
						      "Repeated HTTP shutdown is invalid"});
		if (state_->shutdown_running)
			return Result<bool>::failure({"HTTP_RUNTIME.SHUTDOWN_IN_PROGRESS",
						      "Concurrent HTTP shutdown is invalid"});
		for (const auto &worker : state_->workers)
			if (worker.get_id() == std::this_thread::get_id())
				return Result<bool>::failure(
				    {"HTTP_RUNTIME.SHUTDOWN_FROM_WORKER",
				     "HTTP shutdown must run on the composition thread"});
		state_->shutdown_running = true;
		state_->closing = true;
		state_->cancellation.cancel();
		workers.swap(state_->workers);
	}
	for (auto &worker : workers)
		if (worker.joinable())
			worker.join();
	std::lock_guard<std::mutex> lock(state_->mutex);
	state_->shutdown_running = false;
	if (state_->handles)
		return Result<bool>::failure({"HTTP_RUNTIME.HANDLES_LIVE",
					      "Destroy every HTTP handle before global cleanup"});
	std::lock_guard<std::mutex> process_lock(process_mutex);
	curl_global_cleanup();
	process_live = false;
	state_->closed = true;
	return Result<bool>::success(true);
}
} // namespace bhp::http_runtime
