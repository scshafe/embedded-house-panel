#include "bug_reporter/inbox_client.h"
#include "bhp/durable_store/durable_store.h"
#include "bug_reporter/report_bundle.h"
#include "bug_reporter/report_outbox.h"
#include <fcntl.h>
#include <nlohmann/json.hpp>
#include <regex>
#include <sys/stat.h>
#include <unistd.h>

namespace bug_reporter
{
namespace
{
using Json = nlohmann::json;
using bhp::http_runtime::HttpMethod;
using bhp::http_runtime::HttpRequest;

Error status_error(long status)
{
	if (status == 409)
		return {"REPORT.INBOX_INCOMPLETE", "The inbox is missing part of the bundle", {}, true};
	if (status >= 500 || status == 429)
		return {"REPORT.INBOX_BUSY", "The inbox cannot take it now", {{"http_status", std::to_string(status)}}, true};
	// 401 and 403 are about the panel's identity, not this request, and 416 means the
	// inbox holds a different offset, which the next attempt reads again: all retryable.
	const bool retryable = status == 401 || status == 403 || status == 416;
	return {"REPORT.INBOX_REFUSED", "The inbox refused the request", {{"http_status", std::to_string(status)}},
		retryable};
}

std::optional<std::uint64_t> received_bytes(const std::string &body)
{
	const Json value = Json::parse(body, nullptr, false);
	if (!value.is_object() || !value.contains("received_bytes") || !value["received_bytes"].is_number_unsigned())
		return std::nullopt;
	return value["received_bytes"].get<std::uint64_t>();
}

bool file_name(const std::string &name)
{
	return name == "logs.jsonl.gz" || name == "description.wav";
}

bool fingerprint_text(const std::string &value)
{
	static const std::regex pattern("[0-9a-f]{64}");
	return std::regex_match(value, pattern);
}

bhp::durable_store::Error to_store(const Error &error)
{
	return {error.code, error.message, error.context, error.retryable};
}
} // namespace

InboxClient::InboxClient(InboxSettings settings, HttpTransport transport)
	: settings_(std::move(settings)), transport_(std::move(transport))
{
}

Result<bhp::http_runtime::HttpResponse> InboxClient::exchange(HttpRequest request)
{
	using Output = Result<bhp::http_runtime::HttpResponse>;
	request.budget = settings_.budget;
	request.headers.emplace_back("Accept", "application/json");
	auto response = transport_(request);
	if (!response.has_value())
		return Output::failure({"REPORT.INBOX_UNREACHABLE", "The inbox did not answer",
					{{"http_code", response.error().code}}, true});
	return Output::success(response.value());
}

Result<bool> InboxClient::upload_file(const std::string &report_id, const std::string &name, const std::string &path)
{
	if (!valid_report_id(report_id) || !file_name(name))
		return Result<bool>::failure({"REPORT.UPLOAD_INVALID", "Not a bundle file", {}, false});
	const int descriptor = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
	struct stat info {};
	if (descriptor < 0 || ::fstat(descriptor, &info) != 0) {
		if (descriptor >= 0)
			::close(descriptor);
		return Result<bool>::failure({"REPORT.STORAGE_UNAVAILABLE", "A bundle file is missing", {{"file", name}}, false});
	}
	const auto total = static_cast<std::uint64_t>(info.st_size);
	const auto url = settings_.base_url + "/v1/reports/" + report_id + "/files/" + name;
	bhp::durable_store::UploadPorts ports;
	ports.server_offset = [&]() -> bhp::durable_store::Result<std::uint64_t> {
		HttpRequest request;
		request.method = HttpMethod::Get;
		request.url = url;
		auto response = exchange(request);
		if (!response.has_value())
			return bhp::durable_store::Result<std::uint64_t>::failure(to_store(response.error()));
		if (response.value().status == 404)
			return bhp::durable_store::Result<std::uint64_t>::success(0);
		const auto bytes = received_bytes(response.value().body);
		if (response.value().status != 200 || !bytes)
			return bhp::durable_store::Result<std::uint64_t>::failure(to_store(status_error(response.value().status)));
		return bhp::durable_store::Result<std::uint64_t>::success(*bytes);
	};
	ports.put_range = [&](std::uint64_t offset, std::uint64_t length) -> bhp::durable_store::Result<bhp::durable_store::Done> {
		HttpRequest request;
		request.method = HttpMethod::Put;
		request.url = url;
		request.headers = {{"Content-Type", "application/octet-stream"},
				   {"Content-Range", "bytes " + std::to_string(offset) + "-" +
							     std::to_string(offset + length - 1) + "/" + std::to_string(total)}};
		request.file_body = bhp::http_runtime::FileRange{descriptor, offset, length};
		auto response = exchange(request);
		if (!response.has_value())
			return bhp::durable_store::Result<bhp::durable_store::Done>::failure(to_store(response.error()));
		if (response.value().status != 200 && response.value().status != 201)
			return bhp::durable_store::Result<bhp::durable_store::Done>::failure(to_store(status_error(response.value().status)));
		return bhp::durable_store::Result<bhp::durable_store::Done>::success({});
	};
	auto created = bhp::durable_store::ResumableUpload::create(total, settings_.chunk_bytes, ports);
	if (!created.has_value()) {
		::close(descriptor);
		return Result<bool>::failure({created.error().code, created.error().message, {}, false});
	}
	auto upload = created.value();
	Result<bool> result = Result<bool>::success(true);
	// An empty file is complete as soon as the inbox reports where it stands.
	for (int steps = 0; !upload.complete() && steps < 4096; ++steps) {
		auto step = upload.step();
		if (!step.has_value()) {
			result = Result<bool>::failure({step.error().code, step.error().message, step.error().context, step.error().retryable});
			break;
		}
	}
	::close(descriptor);
	return result;
}

Result<bool> InboxClient::complete(const std::string &report_id, const std::string &manifest)
{
	if (!valid_report_id(report_id))
		return Result<bool>::failure({"REPORT.UPLOAD_INVALID", "Not a report ID", {}, false});
	HttpRequest request;
	request.method = HttpMethod::Post;
	request.url = settings_.base_url + "/v1/reports/" + report_id + "/complete";
	request.headers = {{"Content-Type", "application/json"}};
	request.body = manifest;
	auto response = exchange(request);
	if (!response.has_value())
		return Result<bool>::failure(response.error());
	if (response.value().status != 200 && response.value().status != 201)
		return Result<bool>::failure(status_error(response.value().status));
	return Result<bool>::success(true);
}

Result<bool> InboxClient::send_count(const std::string &fingerprint, const std::string &batch, std::uint64_t count)
{
	static const std::regex batch_pattern("[0-9a-f]{32}");
	if (!fingerprint_text(fingerprint) || !std::regex_match(batch, batch_pattern) || count == 0 ||
	    count > kMaximumCountBatch)
		return Result<bool>::failure({"REPORT.UPLOAD_INVALID", "Not a fingerprint count", {}, false});
	HttpRequest request;
	request.method = HttpMethod::Post;
	request.url = settings_.base_url + "/v1/fingerprints/" + fingerprint + "/count";
	request.headers = {{"Content-Type", "application/json"}};
	request.body = Json({{"count", count}, {"batch", batch}}).dump();
	auto response = exchange(request);
	if (!response.has_value())
		return Result<bool>::failure(response.error());
	if (response.value().status != 200)
		return Result<bool>::failure(status_error(response.value().status));
	return Result<bool>::success(true);
}

Result<std::vector<RemoteStatus>> InboxClient::statuses(const std::vector<std::string> &report_ids)
{
	using Output = Result<std::vector<RemoteStatus>>;
	std::string ids;
	for (const auto &id : report_ids) {
		if (!valid_report_id(id))
			return Output::failure({"REPORT.UPLOAD_INVALID", "Not a report ID", {}, false});
		ids += (ids.empty() ? "" : ",") + id;
	}
	HttpRequest request;
	request.method = HttpMethod::Get;
	request.url = settings_.base_url + "/v1/reports/status?ids=" + ids;
	auto response = exchange(request);
	if (!response.has_value())
		return Output::failure(response.error());
	if (response.value().status != 200)
		return Output::failure(status_error(response.value().status));
	const Json body = Json::parse(response.value().body, nullptr, false);
	if (!body.is_object() || !body.contains("reports") || !body["reports"].is_array())
		return Output::failure({"REPORT.INBOX_ANSWER_INVALID", "The inbox's status answer is malformed", {}, false});
	std::vector<RemoteStatus> statuses;
	for (const auto &item : body["reports"]) {
		if (!item.is_object() || !item.contains("report_id") || !item["report_id"].is_string() ||
		    !item.contains("status") || !item["status"].is_string() || !item.contains("pr"))
			return Output::failure({"REPORT.INBOX_ANSWER_INVALID", "The inbox's status answer is malformed", {}, false});
		RemoteStatus status{item["report_id"].get<std::string>(), item["status"].get<std::string>(), std::nullopt};
		if (item["pr"].is_number_unsigned())
			status.pr = item["pr"].get<std::uint64_t>();
		else if (!item["pr"].is_null())
			return Output::failure({"REPORT.INBOX_ANSWER_INVALID", "The inbox's status answer is malformed", {}, false});
		statuses.push_back(status);
	}
	return Output::success(std::move(statuses));
}
} // namespace bug_reporter
