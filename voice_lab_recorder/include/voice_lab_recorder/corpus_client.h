#pragma once
#include "bhp/http_runtime/http_request.h"
#include "voice_lab_recorder/result.h"
#include <cstdint>
#include <functional>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

/**
 * @canonical voice_lab_corpus_client -- the panel's side of corpus-client.v1 sections 4–7.
 *
 * The intake (declare, resumable `PUT` with `Content-Range`, finalize, the
 * session's state), decisions with stable request IDs, and the panel's views
 * of the library, each `GET` with its `ETag`. Every `/api/` request carries the
 * session's access token; a `401` refreshes it once and asks again. Answers are
 * classified as PROTOCOL.md section 7 says:
 * - VOICE_LAB.API_RETRYABLE (retryable): a transport failure, 408, 429, 5xx,
 *   `409 session_busy`, `503 catalog_unavailable` or `library_unavailable`;
 * - VOICE_LAB.API_UNAUTHORIZED: still `401` after a refresh, so sign in again;
 * - VOICE_LAB.API_REFUSED: any other refusal, with `status` and the refusal's
 *   `error` code in the context. It is not retried.
 * Nothing here logs; no token, body or URL enters an error.
 */
namespace voice_lab_recorder
{
using ApiTransport = std::function<bhp::http_runtime::Result<bhp::http_runtime::HttpResponse>(
	const bhp::http_runtime::HttpRequest &)>;

/** The access token port: `VoiceLabSession` in production. */
struct AccessTokens {
	std::function<Result<std::string>()> current;
	/** Forget the access token so that `current` refreshes; called after a 401. */
	std::function<void()> invalidate;
};

struct CorpusClientSettings {
	/** The PC's API, `https://HOST`, with no trailing slash. */
	std::string api_url;
	/** Reads, declarations, decisions. */
	bhp::http_runtime::TransportBudget request_budget{5000, 30000, 1 << 20};
	/** A chunk's `PUT` and the finalize, which re-hashes the package (the door allows 600 s). */
	bhp::http_runtime::TransportBudget transfer_budget{10000, 600000, 65536};
	/** PROTOCOL.md: at most 64 MiB, 4–8 MiB best. */
	std::uint64_t chunk_bytes{4ULL << 20};
	std::string trusted_ca_pem_for_tests;
};

/** A `GET` with its `ETag`: 200 with a body, or 304 with none. */
struct ViewAnswer {
	bool not_modified{false};
	std::string etag;
	nlohmann::json body;
};

struct DecisionRequest {
	/** `req-` and 32 lower-case hex digits, the same on every retry of this change. */
	std::string request_id;
	/** `draft`, `kept`, `rejected` or `removed`. */
	std::string decision;
	std::uint64_t expected_revision{0};
	/** With a rejection, and only a rejection. */
	std::optional<std::string> reason;
};

/** The IntakeSessionRequest for a package: its five files in sorted order, hashed. */
Result<nlohmann::json> intake_session_request(const std::string &package_directory);
/** A new `req-<32 hex>` from the system's random source. */
Result<std::string> new_request_id();

class CorpusClient final
{
public:
	CorpusClient(CorpusClientSettings settings, ApiTransport transport, AccessTokens tokens);

	Result<ViewAnswer> catalog(const std::string &etag = {});
	/** The panel's view, `?profile=xvf3800-linear-4`. */
	Result<ViewAnswer> library(const std::string &etag = {});
	Result<ViewAnswer> history(const std::string &prompt_id, const std::string &etag = {});

	/** POST the session: 201 for a new one, 200 for one already open. Answers IntakeSessionStatus. */
	Result<nlohmann::json> declare(const std::string &package_directory);
	Result<nlohmann::json> session(const std::string &take_id);
	/**
	 * Sends one file of the package from the bytes the server holds, chunk by
	 * chunk, until the server has it all. A retryable failure returns with the
	 * server's copy as it is; the next call resumes from the server's offset.
	 */
	Result<bool> send_file(const std::string &package_directory, const std::string &take_id, const std::string &path);
	/** Every file the session lacks, then the finalize. Answers the session after the finalize. */
	Result<nlohmann::json> upload(const std::string &package_directory);
	Result<nlohmann::json> finalize(const std::string &take_id);

	/** POST the decision: 202 with the request's status (`queued`). */
	Result<nlohmann::json> decide(const std::string &take_id, const DecisionRequest &request);
	Result<nlohmann::json> request_status(const std::string &request_id);

private:
	struct Exchange {
		long status{0};
		std::string etag;
		nlohmann::json body;
	};
	Result<Exchange> exchange(bhp::http_runtime::HttpRequest request, bool transfer);
	Result<ViewAnswer> view(const std::string &path, const std::string &etag);
	Result<nlohmann::json> json_call(bhp::http_runtime::HttpMethod method, const std::string &path,
					 const std::string &body, std::initializer_list<long> accepted);

	CorpusClientSettings settings_;
	ApiTransport transport_;
	AccessTokens tokens_;
};
} // namespace voice_lab_recorder
