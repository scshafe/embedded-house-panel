#pragma once
#include "bhp/http_runtime/http_runtime.h"
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>
namespace bhp::http_runtime
{
enum class HttpMethod { Get, Post, Put };

/** A body read from a byte range of a caller-owned open file (a chunk of a take). */
struct FileRange {
	int descriptor{-1};
	std::uint64_t offset{0};
	std::uint64_t length{0};
};

/**
 * One request. Header names come from a fixed set (`Accept`, `Authorization`,
 * `Content-Range`, `Content-Type`, `If-None-Match`, `X-Request-ID`) and values
 * hold no control characters. GET carries no body; POST carries `body`; PUT
 * carries `body` or `file_body`. The budget allows up to 60 s to connect and
 * 600 s in all; the response ceiling stays whatever the caller sets.
 */
struct HttpRequest {
	HttpMethod method{HttpMethod::Get};
	std::string url;
	std::vector<std::pair<std::string, std::string>> headers;
	std::string body;
	std::optional<FileRange> file_body;
	TransportBudget budget;
	/** Test-only trust anchor (PEM); production uses the system store. */
	std::string trusted_ca_pem_for_tests;
};

struct HttpResponse {
	long status{0};
	std::string content_type;
	/** The ETag header as sent, or empty. */
	std::string etag;
	std::string body;
};

inline constexpr long kMaximumConnectTimeoutMs = 60'000;
inline constexpr long kMaximumRequestTimeoutMs = 600'000;

/** HTTP.REQUEST_INVALID for a header, method/body or budget outside the rules above. */
Result<bool> validate_request(const HttpRequest &request);

/**
 * @canonical http_request -- one bounded HTTP exchange on the process runtime.
 *
 * One attempt; retry policy belongs to the caller's domain. TLS verification
 * is on, redirects are never followed (a 3xx comes back as a status), and the
 * response body stops at the budget's ceiling (HTTP.RESPONSE_TOO_LARGE).
 * Cancellation of `cancel` or of the runtime ends the exchange
 * (HTTP.CANCELLED), including mid-upload. Transport failures are
 * HTTP.TIMEOUT, HTTP.TLS_VERIFICATION_FAILED or HTTP.TRANSPORT_FAILED, with
 * `retryable` and the libcurl code set. Any status, 4xx and 5xx included, is a
 * response, not an error.
 */
Result<HttpResponse> perform(HttpRuntime &runtime, const HttpRequest &request, CancellationToken cancel);
} // namespace bhp::http_runtime
