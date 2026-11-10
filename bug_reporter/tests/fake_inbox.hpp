// An in-memory PC inbox for the bug_reporter tests: the v1 paths of
// inbox_client.h, checking chunks, manifests and hashes as the receiver must.
#pragma once
#include "bug_reporter/inbox_client.h"
#include "bug_reporter/report_bundle.h"
#include <iostream>
#include <map>
#include <nlohmann/json.hpp>
#include <regex>
#include <set>
#include <string>
#include <unistd.h>
#include <vector>

namespace bug_reporter_test
{
using bhp::http_runtime::HttpMethod;
using bhp::http_runtime::HttpRequest;
using bhp::http_runtime::HttpResponse;
using Json = nlohmann::json;

inline int failures = 0;
inline void expect(bool value, const std::string &label)
{
	if (!value) {
		++failures;
		std::cerr << "FAIL: " << label << '\n';
	}
}

inline std::string temporary_directory(const char *prefix)
{
	std::string pattern = std::string("/tmp/") + prefix + "-XXXXXX";
	if (!::mkdtemp(pattern.data()))
		std::exit(2);
	return pattern;
}

struct FakeInbox {
	std::string base{"https://inbox.example.ts.net:8743"};
	std::map<std::string, std::map<std::string, std::string>> files;
	std::map<std::string, Json> completed;
	std::map<std::string, std::uint64_t> counts;
	/** Count-update batch IDs already added, as the receiver remembers them. */
	std::set<std::string> batches;
	/** Applies the next count update but loses its answer, as a dropped connection would. */
	bool lose_next_count_answer{false};
	/** Reports and fingerprints this inbox refuses with a 400, as it would a malformed bundle. */
	std::set<std::string> refuse;
	std::map<std::string, std::pair<std::string, std::optional<std::uint64_t>>> statuses;
	std::vector<std::string> calls;
	bool unreachable{false};
	/** Answers 503 to this many requests, then works. */
	int busy{0};
	/** Answers 403 to this many requests, as for a panel whose tailnet tag is missing. */
	int forbidden{0};
	/** Keeps only this many bytes of the next PUT, as a connection lost mid-chunk would. */
	std::optional<std::size_t> truncate_next_put;

	static bhp::http_runtime::Result<HttpResponse> answer(long status, const Json &body = Json::object())
	{
		return bhp::http_runtime::Result<HttpResponse>::success({status, "application/json", {}, body.dump()});
	}
	static std::string read_range(const HttpRequest &request)
	{
		if (!request.file_body)
			return request.body;
		std::string bytes(request.file_body->length, '\0');
		const auto got = ::pread(request.file_body->descriptor, bytes.data(), bytes.size(),
					 static_cast<off_t>(request.file_body->offset));
		bytes.resize(got > 0 ? static_cast<std::size_t>(got) : 0);
		return bytes;
	}
	bhp::http_runtime::Result<HttpResponse> operator()(const HttpRequest &request)
	{
		const std::string method = request.method == HttpMethod::Get ? "GET" : request.method == HttpMethod::Post ? "POST" : "PUT";
		const auto path = request.url.substr(base.size());
		calls.push_back(method + " " + path);
		if (unreachable)
			return bhp::http_runtime::Result<HttpResponse>::failure({"HTTP.TRANSPORT_FAILED", "down", true, 7});
		if (busy > 0) {
			--busy;
			return answer(503);
		}
		if (forbidden > 0) {
			--forbidden;
			return answer(403);
		}
		std::smatch match;
		static const std::regex file_path("/v1/reports/(rpt-[0-9a-f]{32})/files/([a-z.]+)");
		static const std::regex complete_path("/v1/reports/(rpt-[0-9a-f]{32})/complete");
		static const std::regex count_path("/v1/fingerprints/([0-9a-f]{64})/count");
		if (std::regex_match(path, match, file_path)) {
			if (refuse.count(match[1]))
				return answer(400);
			auto &held = files[match[1]][match[2]];
			if (method == "GET")
				return held.empty() && !files[match[1]].count(match[2]) ? answer(404) : answer(200, {{"received_bytes", held.size()}});
			std::string range;
			for (const auto &[name, value] : request.headers)
				if (name == "Content-Range")
					range = value;
			static const std::regex range_pattern(R"(bytes (\d+)-(\d+)/(\d+))");
			std::smatch r;
			if (!std::regex_match(range, r, range_pattern) || std::stoull(r[1]) != held.size())
				return answer(416);
			auto bytes = read_range(request);
			if (truncate_next_put) {
				bytes.resize(std::min(bytes.size(), *truncate_next_put));
				truncate_next_put.reset();
				held += bytes;
				return bhp::http_runtime::Result<HttpResponse>::failure({"HTTP.TRANSPORT_FAILED", "reset", true, 56});
			}
			held += bytes;
			return answer(200, {{"received_bytes", held.size()}});
		}
		if (std::regex_match(path, match, complete_path)) {
			const Json manifest = Json::parse(request.body, nullptr, false);
			if (!manifest.is_object() || manifest.value("report_id", std::string()) != std::string(match[1]))
				return answer(400);
			for (const auto &file : manifest["files"]) {
				const auto &held = files[match[1]][file["name"].get<std::string>()];
				if (held.size() != file["bytes"].get<std::uint64_t>() ||
				    bug_reporter::sha256_hex(held) != file["sha256"].get<std::string>())
					return answer(409);
			}
			completed[match[1]] = manifest;
			if (!statuses.count(match[1]))
				statuses[match[1]] = {"received", std::nullopt};
			return answer(201, {{"status", "received"}});
		}
		if (std::regex_match(path, match, count_path)) {
			const Json update = Json::parse(request.body);
			const auto count = update["count"].get<std::uint64_t>();
			if (refuse.count(match[1]) || count < 1 || count > 10'000 || !update.contains("batch"))
				return answer(400);
			if (batches.insert(update["batch"].get<std::string>()).second)
				counts[match[1]] += count;
			if (lose_next_count_answer) {
				lose_next_count_answer = false;
				return bhp::http_runtime::Result<HttpResponse>::failure({"HTTP.TRANSPORT_FAILED", "reset", true, 56});
			}
			return answer(200, {{"count", counts[match[1]]}});
		}
		if (path.rfind("/v1/reports/status?ids=", 0) == 0) {
			Json reports = Json::array();
			std::string ids = path.substr(std::string("/v1/reports/status?ids=").size());
			std::size_t at = 0;
			while (at <= ids.size()) {
				const auto end = std::min(ids.find(',', at), ids.size());
				const auto id = ids.substr(at, end - at);
				if (statuses.count(id))
					reports.push_back({{"report_id", id},
							   {"status", statuses[id].first},
							   {"pr", statuses[id].second ? Json(*statuses[id].second) : Json(nullptr)}});
				at = end + 1;
			}
			return answer(200, {{"reports", reports}});
		}
		return answer(404);
	}
};
} // namespace bug_reporter_test
