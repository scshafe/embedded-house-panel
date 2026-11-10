#pragma once
#include "bhp/http_runtime/http_request.h"
#include "bug_reporter/result.h"
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

/**
 * @canonical bug_report_inbox_api -- the panel's side of the PC inbox, v1.
 *
 * The receiver (step 4) implements the same paths under `base_url`:
 * - `GET  /v1/reports/{id}/files/{name}`: `{"received_bytes": N}`, 404 for none yet.
 * - `PUT  /v1/reports/{id}/files/{name}`: one chunk with `Content-Range: bytes A-B/TOTAL`;
 *   answers `{"received_bytes": N}`.
 * - `POST /v1/reports/{id}/complete`: the manifest (`report.json`); 200 or 201 once the
 *   inbox holds every file it lists, 409 while one is short.
 * - `POST /v1/fingerprints/{fingerprint}/count`: `{"count": N, "batch": ID}`, repeats not sent
 *   as bundles; N is 1 to 10,000 and ID 32 lowercase hex characters. The inbox remembers a
 *   fingerprint's recent batch IDs and adds a batch it already holds only once.
 * - `GET  /v1/reports/status?ids=a,b`: `{"reports": [{"report_id", "status", "pr"}]}`.
 * Everything is idempotent: a retried request after a lost answer changes nothing.
 * The inbox authenticates the panel by its tailnet node (ADR 0010); there is no
 * token. Failures: REPORT.INBOX_UNREACHABLE (no answer), REPORT.INBOX_BUSY (5xx,
 * 429; retryable), REPORT.INBOX_REFUSED (other 4xx; retryable only for 401, 403 and
 * 416), REPORT.INBOX_INCOMPLETE (409, retryable). A failure that is not retryable is
 * about that one request: the reporter parks the bundle or drops the count and goes on.
 */
namespace bug_reporter
{
using HttpTransport = std::function<bhp::http_runtime::Result<bhp::http_runtime::HttpResponse>(
	const bhp::http_runtime::HttpRequest &)>;

struct InboxSettings {
	/** `https://HOST:PORT` of the inbox on the tailnet (or `http://127.0.0.1:PORT` in tests). */
	std::string base_url;
	bhp::http_runtime::TransportBudget budget{3000, 30000, 65536};
	std::uint64_t chunk_bytes{256 * 1024};
};

struct RemoteStatus {
	std::string report_id;
	std::string status;
	std::optional<std::uint64_t> pr;
};

class InboxClient final
{
public:
	InboxClient(InboxSettings settings, HttpTransport transport);
	/** Sends one file of a bundle, resuming from what the inbox already holds. */
	Result<bool> upload_file(const std::string &report_id, const std::string &name, const std::string &path);
	Result<bool> complete(const std::string &report_id, const std::string &manifest);
	Result<bool> send_count(const std::string &fingerprint, const std::string &batch, std::uint64_t count);
	Result<std::vector<RemoteStatus>> statuses(const std::vector<std::string> &report_ids);

private:
	Result<bhp::http_runtime::HttpResponse> exchange(bhp::http_runtime::HttpRequest request);
	InboxSettings settings_;
	HttpTransport transport_;
};
} // namespace bug_reporter
