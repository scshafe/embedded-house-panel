#pragma once
#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>
struct Curl_easy;
namespace bhp::http_runtime
{
struct Error {
	std::string code, message;
};
template <class T> class Result
{
      public:
	static Result success(T v) { return Result(std::move(v)); }
	static Result failure(Error e) { return Result(std::move(e)); }
	bool has_value() const { return std::holds_alternative<T>(value_); }
	const T &value() const { return std::get<T>(value_); }
	const Error &error() const { return std::get<Error>(value_); }

      private:
	explicit Result(T v) : value_(std::move(v)) {}
	explicit Result(Error e) : value_(std::move(e)) {}
	std::variant<T, Error> value_;
};
/** Shared cancellation; safe to signal from a lifecycle or shutdown thread. */
class CancellationToken
{
      public:
	CancellationToken() : cancelled_(std::make_shared<std::atomic<bool>>(false)) {}
	void cancel() const { cancelled_->store(true); }
	bool cancelled() const
	{
		if (cancelled_->load())
			return true;
		for (const auto &signal : parents_)
			if (signal->load())
				return true;
		return false;
	}
	static CancellationToken linked(const CancellationToken &first,
					const CancellationToken &second)
	{
		CancellationToken result;
		result.parents_ = first.parents_;
		result.parents_.insert(result.parents_.end(), second.parents_.begin(),
				       second.parents_.end());
		result.parents_.push_back(first.cancelled_);
		result.parents_.push_back(second.cancelled_);
		return result;
	}

      private:
	std::shared_ptr<std::atomic<bool>> cancelled_;
	std::vector<std::shared_ptr<std::atomic<bool>>> parents_;
};
struct TransportBudget {
	long connect_timeout_ms{1000}, request_timeout_ms{5000};
	std::size_t maximum_response_bytes{1048576};
};
/** Validate all bounds before allocating any transport handle. */
Result<TransportBudget> validate_budget(TransportBudget budget);
/** @canonical http_runtime -- sole process owner of curl global lifetime. */
class HttpRuntime final
{
      public:
	struct State;
	class Handle final
	{
	      public:
		~Handle();
		void *native_handle() const { return handle_; }

	      private:
		friend class HttpRuntime;
		Handle(std::shared_ptr<State> state, void *handle)
		    : state_(std::move(state)), handle_(handle)
		{
		}
		std::shared_ptr<State> state_;
		void *handle_;
	};
	static Result<std::shared_ptr<HttpRuntime>> create();
	~HttpRuntime();
	/** Fails closed after cancellation or shutdown; handles pin runtime global lifetime. */
	Result<std::shared_ptr<Handle>> create_handle();
	/** Runtime owns and joins every worker before cleanup. Work must honor its token. */
	Result<bool> start_worker(std::function<void(CancellationToken)> work);
	CancellationToken cancellation() const;
	/** Cancel, join workers, then cleanup. Live external handles defer cleanup explicitly. */
	Result<bool> shutdown();

      private:
	explicit HttpRuntime(std::shared_ptr<State> state) : state_(std::move(state)) {}
	std::shared_ptr<State> state_;
};
} // namespace bhp::http_runtime
