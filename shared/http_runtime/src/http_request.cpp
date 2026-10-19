#include "bhp/http_runtime/http_request.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <curl/curl.h>
#include <unistd.h>
namespace bhp::http_runtime
{
namespace
{
const std::array<const char *, 6> kAllowedHeaders{"Accept",	     "Authorization", "Content-Range",
						  "Content-Type", "If-None-Match", "X-Request-ID"};

Error invalid(const char *detail)
{
	return {"HTTP.REQUEST_INVALID", std::string("HTTP request is outside the allowed shape: ") + detail};
}

struct Exchange {
	std::string body;
	std::size_t limit{0};
	bool oversized{false};
	CancellationToken cancel, global;
	// Upload source: a memory body or a file range.
	const std::string *memory{nullptr};
	std::size_t memory_offset{0};
	std::optional<FileRange> file;
	std::uint64_t file_sent{0};
	bool read_failed{false};

	bool cancelled() const { return cancel.cancelled() || global.cancelled(); }
};

std::size_t write_body(char *data, std::size_t size, std::size_t count, void *opaque)
{
	auto &exchange = *static_cast<Exchange *>(opaque);
	if (exchange.cancelled())
		return 0;
	if (count && size > exchange.limit / count) {
		exchange.oversized = true;
		return 0;
	}
	const auto bytes = size * count;
	if (bytes > exchange.limit - exchange.body.size()) {
		exchange.oversized = true;
		return 0;
	}
	exchange.body.append(data, bytes);
	return bytes;
}

std::size_t read_body(char *buffer, std::size_t size, std::size_t count, void *opaque)
{
	auto &exchange = *static_cast<Exchange *>(opaque);
	if (exchange.cancelled())
		return CURL_READFUNC_ABORT;
	const auto room = size * count;
	if (exchange.memory) {
		const auto take = std::min(room, exchange.memory->size() - exchange.memory_offset);
		std::copy_n(exchange.memory->data() + exchange.memory_offset, take, buffer);
		exchange.memory_offset += take;
		return take;
	}
	const auto remaining = exchange.file->length - exchange.file_sent;
	const auto want = static_cast<std::size_t>(std::min<std::uint64_t>(room, remaining));
	if (want == 0)
		return 0;
	ssize_t got;
	do
		got = ::pread(exchange.file->descriptor, buffer, want,
			      static_cast<off_t>(exchange.file->offset + exchange.file_sent));
	while (got < 0 && errno == EINTR);
	if (got <= 0) {
		// A file that shrank or failed mid-upload must not look like a short body.
		exchange.read_failed = true;
		return CURL_READFUNC_ABORT;
	}
	exchange.file_sent += static_cast<std::uint64_t>(got);
	return static_cast<std::size_t>(got);
}

int progress(void *opaque, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{
	return static_cast<Exchange *>(opaque)->cancelled() ? 1 : 0;
}

bool header_name_allowed(const std::string &name)
{
	return std::any_of(kAllowedHeaders.begin(), kAllowedHeaders.end(),
			   [&name](const char *allowed) { return name == allowed; });
}

bool header_value_safe(const std::string &value)
{
	return value.size() <= 8192 && std::none_of(value.begin(), value.end(), [](const unsigned char c) {
		       return c < 0x20 || c == 0x7f;
	       });
}
} // namespace

Result<bool> validate_request(const HttpRequest &request)
{
	if (request.url.rfind("https://", 0) != 0 && request.url.rfind("http://", 0) != 0)
		return Result<bool>::failure(invalid("url scheme"));
	if (!header_value_safe(request.url) || request.url.find(' ') != std::string::npos)
		return Result<bool>::failure(invalid("url characters"));
	for (const auto &[name, value] : request.headers)
		if (!header_name_allowed(name) || !header_value_safe(value))
			return Result<bool>::failure(invalid("header"));
	const bool has_body = !request.body.empty() || request.file_body.has_value();
	if (request.method == HttpMethod::Get && has_body)
		return Result<bool>::failure(invalid("GET with a body"));
	if (request.method == HttpMethod::Post && request.file_body)
		return Result<bool>::failure(invalid("POST from a file"));
	if (request.file_body && (!request.body.empty() || request.file_body->descriptor < 0))
		return Result<bool>::failure(invalid("file body"));
	auto budget = validate_budget(request.budget);
	if (!budget.has_value() || request.budget.connect_timeout_ms > kMaximumConnectTimeoutMs ||
	    request.budget.request_timeout_ms > kMaximumRequestTimeoutMs)
		return Result<bool>::failure(invalid("budget"));
	return Result<bool>::success(true);
}

Result<HttpResponse> perform(HttpRuntime &runtime, const HttpRequest &request, CancellationToken cancel)
{
	using Output = Result<HttpResponse>;
	auto valid = validate_request(request);
	if (!valid.has_value())
		return Output::failure(valid.error());
	Exchange exchange;
	exchange.limit = request.budget.maximum_response_bytes;
	exchange.cancel = cancel;
	exchange.global = runtime.cancellation();
	if (exchange.cancelled())
		return Output::failure({"HTTP.CANCELLED", "HTTP request cancelled"});
	auto allocated = runtime.create_handle();
	if (!allocated.has_value())
		return Output::failure(allocated.error());
	auto *curl = static_cast<CURL *>(allocated.value()->native_handle());

	curl_slist *headers = nullptr;
	for (const auto &[name, value] : request.headers)
		headers = curl_slist_append(headers, (name + ": " + value).c_str());
	// Uploads go out at once; no 100-continue round trip.
	headers = curl_slist_append(headers, "Expect:");
	curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	switch (request.method) {
	case HttpMethod::Get:
		curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
		break;
	case HttpMethod::Post:
		curl_easy_setopt(curl, CURLOPT_POST, 1L);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.data());
		curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(request.body.size()));
		break;
	case HttpMethod::Put:
		curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
		curl_easy_setopt(curl, CURLOPT_READFUNCTION, read_body);
		curl_easy_setopt(curl, CURLOPT_READDATA, &exchange);
		if (request.file_body) {
			exchange.file = request.file_body;
			curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, static_cast<curl_off_t>(request.file_body->length));
		} else {
			exchange.memory = &request.body;
			curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, static_cast<curl_off_t>(request.body.size()));
		}
		break;
	}
	curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, request.budget.request_timeout_ms);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, request.budget.connect_timeout_ms);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_body);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &exchange);
	curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress);
	curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &exchange);
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
	curl_blob ca{const_cast<char *>(request.trusted_ca_pem_for_tests.data()), request.trusted_ca_pem_for_tests.size(),
		     CURL_BLOB_COPY};
	if (!request.trusted_ca_pem_for_tests.empty())
		curl_easy_setopt(curl, CURLOPT_CAINFO_BLOB, &ca);

	const auto status = curl_easy_perform(curl);
	HttpResponse response;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);
	char *type = nullptr;
	curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &type);
	response.content_type = type ? type : "";
	curl_header *etag = nullptr;
	if (curl_easy_header(curl, "ETag", 0, CURLH_HEADER, -1, &etag) == CURLHE_OK && etag && etag->value)
		response.etag = etag->value;
	curl_slist_free_all(headers);

	if (exchange.cancelled())
		return Output::failure({"HTTP.CANCELLED", "HTTP request cancelled"});
	if (exchange.oversized)
		return Output::failure({"HTTP.RESPONSE_TOO_LARGE", "HTTP response exceeded its byte ceiling"});
	if (exchange.read_failed)
		return Output::failure({"HTTP.BODY_READ_FAILED", "HTTP request body could not be read"});
	if (status == CURLE_PEER_FAILED_VERIFICATION || status == CURLE_SSL_ISSUER_ERROR)
		return Output::failure({"HTTP.TLS_VERIFICATION_FAILED", "TLS certificate or hostname verification failed",
					false, static_cast<int>(status)});
	if (status != CURLE_OK) {
		const bool retryable = status == CURLE_COULDNT_CONNECT || status == CURLE_COULDNT_RESOLVE_HOST ||
				       status == CURLE_OPERATION_TIMEDOUT || status == CURLE_RECV_ERROR ||
				       status == CURLE_SEND_ERROR;
		return Output::failure({status == CURLE_OPERATION_TIMEDOUT ? "HTTP.TIMEOUT" : "HTTP.TRANSPORT_FAILED",
					"HTTP transport failed", retryable, static_cast<int>(status)});
	}
	response.body = std::move(exchange.body);
	return Output::success(std::move(response));
}
} // namespace bhp::http_runtime
