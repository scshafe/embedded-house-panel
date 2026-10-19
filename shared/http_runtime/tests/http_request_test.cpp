// The request API against a scriptable loopback server: methods, headers, ETag and
// If-None-Match, Content-Range uploads from a file range, budgets, the response
// ceiling, redirects, timeouts and cancellation mid-upload. TLS verification is
// exercised by the lexinet client's HTTPS tests, which run through perform().
#include "bhp/http_runtime/http_request.h"
#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <netinet/in.h>
#include <poll.h>
#include <random>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
using namespace bhp::http_runtime;
namespace
{
int failures = 0;
void expect(bool value, const std::string &label)
{
	if (!value) {
		++failures;
		std::cerr << "FAIL: " << label << '\n';
	}
}

struct Seen {
	std::string method, target;
	std::map<std::string, std::string> headers; // lower-case names
	std::string body;
};
struct Reply {
	int status{200};
	std::vector<std::pair<std::string, std::string>> headers;
	std::string body;
	std::chrono::milliseconds delay{0};
};

/** One request per connection; reads the whole body (optionally slowly), then replies. */
class Server
{
public:
	explicit Server(std::function<Reply(const Seen &)> handler, std::chrono::milliseconds read_pause = {})
		: handler_(std::move(handler)), read_pause_(read_pause)
	{
		listener_ = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
		sockaddr_in address{};
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		::bind(listener_, reinterpret_cast<sockaddr *>(&address), sizeof address);
		::listen(listener_, 8);
		socklen_t size = sizeof address;
		::getsockname(listener_, reinterpret_cast<sockaddr *>(&address), &size);
		port_ = ntohs(address.sin_port);
		thread_ = std::thread([this] { run(); });
	}
	~Server()
	{
		stop_ = true;
		thread_.join();
		::close(listener_);
	}
	std::string url(const std::string &path) const { return "http://127.0.0.1:" + std::to_string(port_) + path; }
	std::vector<Seen> seen()
	{
		std::lock_guard<std::mutex> lock(mutex_);
		return seen_;
	}

private:
	void run()
	{
		while (!stop_) {
			pollfd waiting{listener_, POLLIN, 0};
			if (::poll(&waiting, 1, 20) <= 0)
				continue;
			const int connection = ::accept(listener_, nullptr, nullptr);
			if (connection >= 0) {
				serve(connection);
				::close(connection);
			}
		}
	}
	bool read_some(int connection, std::string &buffer, std::size_t at_most)
	{
		pollfd ready{connection, POLLIN, 0};
		if (::poll(&ready, 1, 2000) <= 0 || stop_)
			return false;
		char chunk[65536];
		const auto got = ::recv(connection, chunk, std::min(sizeof chunk, at_most), 0);
		if (got <= 0)
			return false;
		buffer.append(chunk, static_cast<std::size_t>(got));
		return true;
	}
	void serve(int connection)
	{
		std::string buffer;
		while (buffer.find("\r\n\r\n") == std::string::npos)
			if (!read_some(connection, buffer, 65536))
				return;
		const auto end = buffer.find("\r\n\r\n");
		Seen request;
		std::size_t line_end = buffer.find("\r\n");
		const auto first = buffer.substr(0, line_end);
		request.method = first.substr(0, first.find(' '));
		request.target = first.substr(first.find(' ') + 1, first.rfind(' ') - first.find(' ') - 1);
		for (std::size_t at = line_end + 2; at < end;) {
			const auto next = buffer.find("\r\n", at);
			const auto line = buffer.substr(at, next - at);
			const auto colon = line.find(':');
			std::string name = line.substr(0, colon);
			for (auto &c : name)
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			request.headers[name] = line.substr(line.find_first_not_of(' ', colon + 1));
			at = next + 2;
		}
		request.body = buffer.substr(end + 4);
		const auto length = request.headers.count("content-length")
					    ? std::stoull(request.headers["content-length"])
					    : 0ULL;
		while (request.body.size() < length) {
			if (read_pause_.count())
				std::this_thread::sleep_for(read_pause_);
			if (!read_some(connection, request.body, read_pause_.count() ? 16384 : 65536))
				return;
		}
		{
			std::lock_guard<std::mutex> lock(mutex_);
			seen_.push_back(request);
		}
		const auto reply = handler_(request);
		std::this_thread::sleep_for(reply.delay);
		std::string text = "HTTP/1.1 " + std::to_string(reply.status) + " X\r\nContent-Length: " +
				   std::to_string(reply.body.size()) + "\r\nConnection: close\r\n";
		for (const auto &[name, value] : reply.headers)
			text += name + ": " + value + "\r\n";
		text += "\r\n" + reply.body;
		(void)::send(connection, text.data(), text.size(), MSG_NOSIGNAL);
	}
	std::function<Reply(const Seen &)> handler_;
	std::chrono::milliseconds read_pause_;
	int listener_{-1};
	int port_{0};
	std::atomic<bool> stop_{false};
	std::thread thread_;
	std::mutex mutex_;
	std::vector<Seen> seen_;
};

HttpRequest request(HttpMethod method, std::string url)
{
	HttpRequest value;
	value.method = method;
	value.url = std::move(url);
	value.budget = {1000, 5000, 1 << 20};
	return value;
}

/** A temporary file of `size` pseudo-random bytes, open for reading. */
std::pair<int, std::string> data_file(std::size_t size)
{
	char path[] = "/tmp/http-request-test-XXXXXX";
	const int descriptor = ::mkstemp(path);
	::unlink(path);
	std::string data(size, '\0');
	std::mt19937 random(42);
	for (auto &c : data)
		c = static_cast<char>(random() & 0xff);
	if (::write(descriptor, data.data(), data.size()) != static_cast<ssize_t>(data.size()))
		std::abort();
	return {descriptor, data};
}

void test_validation()
{
	auto bad = request(HttpMethod::Get, "http://127.0.0.1:1/");
	bad.headers = {{"Cookie", "x"}};
	expect(!validate_request(bad).has_value(), "a header outside the set");
	bad.headers = {{"X-Request-ID", "a\r\nInjected: 1"}};
	expect(!validate_request(bad).has_value(), "a control character in a value");
	bad.headers = {};
	bad.body = "x";
	expect(!validate_request(bad).has_value(), "GET with a body");
	auto post = request(HttpMethod::Post, "http://127.0.0.1:1/");
	post.file_body = FileRange{0, 0, 1};
	expect(!validate_request(post).has_value(), "POST from a file");
	auto budget = request(HttpMethod::Get, "http://127.0.0.1:1/");
	budget.budget = {60000, 600000, 1 << 20};
	expect(validate_request(budget).has_value(), "600 s with a 60 s connect is allowed");
	budget.budget = {1000, 600001, 1 << 20};
	expect(!validate_request(budget).has_value(), "601 s is refused");
	budget.budget = {60001, 600000, 1 << 20};
	expect(!validate_request(budget).has_value(), "a 60.001 s connect is refused");
	expect(!validate_request(request(HttpMethod::Get, "ftp://host/")).has_value(), "other schemes");
}

void test_get_with_etag(HttpRuntime &runtime)
{
	Server server([](const Seen &seen) {
		if (seen.headers.count("if-none-match") && seen.headers.at("if-none-match") == "\"catalog-7\"")
			return Reply{304, {{"ETag", "\"catalog-7\""}}, {}};
		return Reply{200, {{"ETag", "\"catalog-7\""}, {"Content-Type", "application/json"}}, "{\"prompts\":[]}"};
	});
	auto first = perform(runtime, request(HttpMethod::Get, server.url("/api/v1/catalog")), {});
	expect(first.has_value() && first.value().status == 200 && first.value().etag == "\"catalog-7\"" &&
		       first.value().body == "{\"prompts\":[]}" && first.value().content_type == "application/json",
	       "GET returns the body and its ETag");
	auto conditional = request(HttpMethod::Get, server.url("/api/v1/catalog"));
	conditional.headers = {{"If-None-Match", "\"catalog-7\""}};
	auto second = perform(runtime, conditional, {});
	expect(second.has_value() && second.value().status == 304 && second.value().body.empty(),
	       "If-None-Match is answered 304 as a response, not an error");
	expect(server.seen().size() == 2 && server.seen()[1].method == "GET", "two GETs reached the server");
}

void test_post_and_put(HttpRuntime &runtime)
{
	Server server([](const Seen &) { return Reply{201, {{"Content-Type", "application/json"}}, "{}"}; });
	auto post = request(HttpMethod::Post, server.url("/api/v1/intake/sessions"));
	post.headers = {{"Content-Type", "application/json"}, {"X-Request-ID", "trace-1"}};
	post.body = "{\"files\":[]}";
	auto posted = perform(runtime, post, {});
	auto put = request(HttpMethod::Put, server.url("/memory"));
	put.body = "small body";
	auto memory = perform(runtime, put, {});
	const auto seen = server.seen();
	expect(posted.has_value() && posted.value().status == 201 && seen.size() == 2 && seen[0].method == "POST" &&
		       seen[0].body == post.body && seen[0].headers.at("x-request-id") == "trace-1",
	       "POST sends its body and headers");
	expect(memory.has_value() && seen[1].method == "PUT" && seen[1].body == "small body" &&
		       !seen[1].headers.count("expect"),
	       "PUT sends a memory body with no 100-continue");
}

void test_chunked_upload_from_a_file(HttpRuntime &runtime)
{
	const std::size_t total = 3 * (1 << 20) + 12345;
	auto [descriptor, data] = data_file(total);
	std::mutex mutex;
	std::string assembled;
	Server server([&](const Seen &seen) {
		std::lock_guard<std::mutex> lock(mutex);
		const auto range = seen.headers.count("content-range") ? seen.headers.at("content-range") : "";
		const auto expected = "bytes " + std::to_string(assembled.size()) + "-" +
				      std::to_string(assembled.size() + seen.body.size() - 1) + "/" + std::to_string(total);
		if (range != expected)
			return Reply{416, {}, {}};
		assembled += seen.body;
		return Reply{200, {}, {}};
	});
	const std::uint64_t chunk = 1 << 20;
	bool all_accepted = true;
	for (std::uint64_t offset = 0; offset < total; offset += chunk) {
		const auto length = std::min<std::uint64_t>(chunk, total - offset);
		auto put = request(HttpMethod::Put, server.url("/api/v1/intake/sessions/s/files/original.wav"));
		put.headers = {{"Content-Range", "bytes " + std::to_string(offset) + "-" +
							 std::to_string(offset + length - 1) + "/" + std::to_string(total)}};
		put.file_body = FileRange{descriptor, offset, length};
		auto response = perform(runtime, put, {});
		all_accepted = all_accepted && response.has_value() && response.value().status == 200;
	}
	expect(all_accepted && assembled == data && server.seen().size() == 4,
	       "four Content-Range chunks arrive byte for byte");
	auto beyond = request(HttpMethod::Put, server.url("/beyond"));
	beyond.file_body = FileRange{descriptor, total - 10, 100};
	auto short_read = perform(runtime, beyond, {});
	expect(!short_read.has_value() && short_read.error().code == "HTTP.BODY_READ_FAILED",
	       "a range past the end of the file fails instead of sending a short body");
	::close(descriptor);
}

void test_limits(HttpRuntime &runtime)
{
	Server large([](const Seen &) { return Reply{200, {}, std::string(4096, 'x')}; });
	auto capped = request(HttpMethod::Get, large.url("/"));
	capped.budget.maximum_response_bytes = 1024;
	auto too_large = perform(runtime, capped, {});
	expect(!too_large.has_value() && too_large.error().code == "HTTP.RESPONSE_TOO_LARGE", "response ceiling");

	Server redirect([](const Seen &) { return Reply{302, {{"Location", "/elsewhere"}}, {}}; });
	auto redirected = perform(runtime, request(HttpMethod::Get, redirect.url("/")), {});
	expect(redirected.has_value() && redirected.value().status == 302 && redirect.seen().size() == 1,
	       "a redirect comes back as a status and is not followed");

	Server slow([](const Seen &) { return Reply{200, {}, "late", std::chrono::milliseconds(1500)}; });
	auto hurried = request(HttpMethod::Get, slow.url("/"));
	hurried.budget = {200, 300, 1 << 20};
	auto timed_out = perform(runtime, hurried, {});
	expect(!timed_out.has_value() && timed_out.error().code == "HTTP.TIMEOUT" && timed_out.error().retryable,
	       "a slow server times out, retryably");

	auto refused = request(HttpMethod::Get, "http://127.0.0.1:1/");
	auto connect = perform(runtime, refused, {});
	expect(!connect.has_value() && connect.error().code == "HTTP.TRANSPORT_FAILED" && connect.error().retryable &&
		       connect.error().transport_code != 0,
	       "a refused connection is a retryable transport failure");
}

void test_cancellation(HttpRuntime &runtime)
{
	CancellationToken already;
	already.cancel();
	auto early = perform(runtime, request(HttpMethod::Get, "http://127.0.0.1:1/"), already);
	expect(!early.has_value() && early.error().code == "HTTP.CANCELLED", "a cancelled token sends nothing");

	auto [descriptor, data] = data_file(16 << 20);
	Server slow_reader([](const Seen &) { return Reply{200, {}, {}}; }, std::chrono::milliseconds(20));
	auto put = request(HttpMethod::Put, slow_reader.url("/upload"));
	put.file_body = FileRange{descriptor, 0, data.size()};
	put.budget = {1000, 60000, 1 << 20};
	CancellationToken cancel;
	std::thread canceller([cancel] {
		std::this_thread::sleep_for(std::chrono::milliseconds(150));
		cancel.cancel();
	});
	const auto started = std::chrono::steady_clock::now();
	auto stopped = perform(runtime, put, cancel);
	const auto took = std::chrono::steady_clock::now() - started;
	canceller.join();
	expect(!stopped.has_value() && stopped.error().code == "HTTP.CANCELLED" && took < std::chrono::seconds(3),
	       "cancellation ends an upload in progress");
	::close(descriptor);
}
} // namespace

int main()
{
	auto runtime = HttpRuntime::create();
	if (!runtime.has_value()) {
		std::cerr << "runtime unavailable\n";
		return 1;
	}
	test_validation();
	test_get_with_etag(*runtime.value());
	test_post_and_put(*runtime.value());
	test_chunked_upload_from_a_file(*runtime.value());
	test_limits(*runtime.value());
	test_cancellation(*runtime.value());
	(void)runtime.value()->shutdown();
	std::cout << "http_request: " << failures << " failures\n";
	return failures ? 1 : 0;
}
