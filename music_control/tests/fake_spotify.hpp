// A scripted Spotify for the music_control tests: the token endpoint and the
// Web API answer from queues of fixture bodies, and every request is kept so
// a test can check what the client sent. No network.
#pragma once
#include "music_control/spotify_auth.h"
#include <chrono>
#include <deque>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace music_control_test
{
using bhp::http_runtime::HttpMethod;
using bhp::http_runtime::HttpRequest;
using bhp::http_runtime::HttpResponse;

inline int failures = 0;
inline void expect(bool value, const std::string &label)
{
	if (!value) {
		++failures;
		std::cerr << "FAIL: " << label << '\n';
	}
}

inline std::string fixture(const std::string &name)
{
	std::ifstream input(std::string(BHP_SPOTIFY_FIXTURES) + "/" + name, std::ios::binary);
	if (!input) {
		std::cerr << "missing fixture " << name << '\n';
		std::exit(2);
	}
	return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

inline const std::string kTokenUrl = "https://accounts.spotify.com/api/token";
inline const std::string kApiUrl = "https://api.spotify.com/v1";
inline const std::string kClientId = "0123456789abcdef0123456789abcdef";

struct Answer {
	long status{200};
	std::string body;
};

struct FakeSpotify {
	std::mutex mutex;
	std::vector<HttpRequest> requests;
	std::deque<Answer> token_answers;
	/** Keyed by "METHOD path", the path after /v1 with its query. */
	std::map<std::string, std::deque<Answer>> api_answers;
	bool unreachable{false};
	std::chrono::milliseconds token_delay{0};

	static std::string method_name(HttpMethod method)
	{
		return method == HttpMethod::Get ? "GET" : method == HttpMethod::Post ? "POST" : "PUT";
	}
	void token(long status, const std::string &body) { token_answers.push_back({status, body}); }
	void api(const std::string &key, long status, const std::string &body = {})
	{
		api_answers[key].push_back({status, body});
	}
	/** Requests to the Web API, as "METHOD path". */
	std::vector<std::string> api_calls()
	{
		std::lock_guard<std::mutex> lock(mutex);
		std::vector<std::string> calls;
		for (const auto &request : requests)
			if (request.url.rfind(kApiUrl, 0) == 0)
				calls.push_back(method_name(request.method) + " " + request.url.substr(kApiUrl.size()));
		return calls;
	}
	std::size_t token_requests()
	{
		std::lock_guard<std::mutex> lock(mutex);
		std::size_t count = 0;
		for (const auto &request : requests)
			count += request.url == kTokenUrl;
		return count;
	}
	bhp::http_runtime::Result<HttpResponse> operator()(const HttpRequest &request)
	{
		std::unique_lock<std::mutex> lock(mutex);
		requests.push_back(request);
		if (unreachable)
			return bhp::http_runtime::Result<HttpResponse>::failure({"HTTP.TRANSPORT_FAILED", "down", true, 7});
		Answer answer{500, R"({"error":{"status":500,"message":"unscripted"}})"};
		if (request.url == kTokenUrl) {
			if (!token_answers.empty()) {
				answer = token_answers.front();
				token_answers.pop_front();
			}
			lock.unlock();
			std::this_thread::sleep_for(token_delay);
		} else {
			const auto key = method_name(request.method) + " " + request.url.substr(kApiUrl.size());
			auto found = api_answers.find(key);
			if (found != api_answers.end() && !found->second.empty()) {
				answer = found->second.front();
				found->second.pop_front();
			} else
				answer = {404, R"({"error":{"status":404,"message":"unscripted"}})"};
		}
		return bhp::http_runtime::Result<HttpResponse>::success(
			{answer.status, answer.body.empty() ? std::string() : "application/json; charset=utf-8", {}, answer.body});
	}
};

/** Parses a form body into its raw (still encoded) fields. */
inline std::map<std::string, std::string> form(const std::string &body)
{
	std::map<std::string, std::string> fields;
	std::size_t at = 0;
	while (at <= body.size()) {
		const auto end = std::min(body.find('&', at), body.size());
		const auto pair = body.substr(at, end - at);
		fields[pair.substr(0, pair.find('='))] = pair.substr(pair.find('=') + 1);
		at = end + 1;
	}
	return fields;
}

/** A test clock that moves only when told. */
struct ManualClock {
	std::chrono::steady_clock::time_point now{std::chrono::steady_clock::time_point(std::chrono::hours(1))};
	music_control::Clock function()
	{
		return [this] { return now; };
	}
};
} // namespace music_control_test
