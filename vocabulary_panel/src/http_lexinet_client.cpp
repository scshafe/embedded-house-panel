#include "vocabulary_panel/http_lexinet_client.h"
#include "bhp/http_runtime/http_request.h"
#include "bhp/structured_logger.h"
#include "vocabulary_panel/study_wire_codec.h"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <fcntl.h>
#include <regex>
#include <stdexcept>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
namespace vocabulary_panel
{
namespace
{
using Clock = std::chrono::steady_clock;
std::string read_credential(const std::string &path)
{
	if (path.empty())
		return {};
	int fd = open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
	struct stat s{};
	if (fd < 0)
		throw std::runtime_error("LEXINET.CREDENTIAL_FILE_OPEN_FAILED");
	if (fstat(fd, &s) != 0 || !S_ISREG(s.st_mode) || (s.st_uid != geteuid() && s.st_uid != 0) ||
	    (s.st_mode & 0077) != 0 || s.st_size < 1 || s.st_size > 4096) {
		close(fd);
		throw std::runtime_error("LEXINET.CREDENTIAL_FILE_INVALID");
	}
	std::string value(static_cast<std::size_t>(s.st_size), '\0');
	const auto count = read(fd, value.data(), value.size());
	close(fd);
	if (count != s.st_size)
		throw std::runtime_error("LEXINET.CREDENTIAL_FILE_READ_FAILED");
	if (!value.empty() && value.back() == '\n')
		value.pop_back();
	if (value.empty() || value.find_first_of("\r\n") != std::string::npos)
		throw std::runtime_error("LEXINET.CREDENTIAL_FILE_INVALID");
	return value;
}
/** Sleeps 50 ms times the attempt number, up to the deadline; false when cancelled. */
bool wait_before_retry(int attempt, Clock::time_point deadline, const bhp::http_runtime::CancellationToken &local,
		       const bhp::http_runtime::CancellationToken &global)
{
	const auto until = std::min(deadline, Clock::now() + std::chrono::milliseconds(50 * (attempt + 1)));
	while (Clock::now() < until) {
		if (local.cancelled() || global.cancelled())
			return false;
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
	return true;
}
VocabularyPanelError cancelled()
{
	return {"LEXINET.CANCELLED", "Lexinet request cancelled", {}, false};
}
} // namespace
struct HttpLexinetClient::Implementation {
	std::shared_ptr<bhp::http_runtime::HttpRuntime> runtime;
	HttpLexinetConfig config;
	bhp::StructuredLogger &logger;
	Clock::time_point clock_origin{Clock::now()};
	std::string credential;
	Implementation(std::shared_ptr<bhp::http_runtime::HttpRuntime> r, HttpLexinetConfig c,
		       bhp::StructuredLogger &l)
	    : runtime(std::move(r)), config(std::move(c)), logger(l)
	{
		if (!runtime ||
		    !bhp::http_runtime::validate_budget(config.transport_budget).has_value() ||
		    config.transport_budget.maximum_response_bytes > 1048576 ||
		    config.maximum_snapshot_age_seconds == 0)
			throw std::invalid_argument("LEXINET.CONFIG_INVALID");
		const std::regex secure("^https://[A-Za-z0-9.-]+(:[0-9]{1,5})?$");
		const std::regex loopback_http("^http://127\\.0\\.0\\.1:[0-9]{1,5}$");
		const std::regex loopback_https("^https://127\\.0\\.0\\.1:[0-9]{1,5}$");
		if (!std::regex_match(config.lexinet_base_url, secure) &&
		    !(config.allow_insecure_http_for_tests &&
		      std::regex_match(config.lexinet_base_url, loopback_http)))
			throw std::invalid_argument("LEXINET.HTTPS_REQUIRED");
		if (!config.trusted_ca_certificate_pem_for_tests.empty() &&
		    !std::regex_match(config.lexinet_base_url, loopback_https) &&
		    !(config.allow_qemu_host_test_ca &&
		      std::regex_match(config.lexinet_base_url,
				       std::regex("^https://10\\.0\\.2\\.2:[0-9]{1,5}$"))))
			throw std::invalid_argument("LEXINET.TEST_CA_REQUIRES_LOOPBACK_HTTPS");
		if (config.clock_trusted && config.current_utc_seconds <= 0)
			throw std::invalid_argument("LEXINET.CLOCK_CONFIG_INVALID");
		credential = read_credential(config.credential_file_path);
	}
	VocabularyPanelResult<std::string> request(const std::string &path, const std::string &body,
						   const std::string &trace,
						   bhp::http_runtime::CancellationToken local)
	{
		using Result = VocabularyPanelResult<std::string>;
		if (trace.empty() || trace.size() > 128 ||
		    trace.find_first_not_of(
			"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_.") !=
			std::string::npos)
			return Result::failure({"LEXINET.TRACE_ID_INVALID",
						"Trace ID cannot contain header control characters",
						{},
						false});
		auto global = runtime->cancellation();
		const auto started = Clock::now(),
			   deadline = started + std::chrono::milliseconds(
						    config.transport_budget.request_timeout_ms);
		for (int attempt = 0; attempt < 3; ++attempt) {
			if (local.cancelled() || global.cancelled())
				return Result::failure(cancelled());
			const auto remaining =
			    std::chrono::duration_cast<std::chrono::milliseconds>(deadline -
										  Clock::now())
				.count();
			if (remaining <= 0)
				return Result::failure({"LEXINET.TIMEOUT",
							"Lexinet whole-request budget exhausted",
							{},
							true});
			bhp::http_runtime::HttpRequest http_request;
			http_request.method = bhp::http_runtime::HttpMethod::Post;
			http_request.url = config.lexinet_base_url + path;
			http_request.headers = {{"Accept", "application/json"},
						{"Content-Type", "application/json"},
						{"X-Request-ID", trace}};
			if (!credential.empty())
				http_request.headers.emplace_back("Authorization", "Bearer " + credential);
			http_request.body = body;
			http_request.budget = {std::min(config.transport_budget.connect_timeout_ms,
							static_cast<long>(remaining)),
					       static_cast<long>(remaining),
					       config.transport_budget.maximum_response_bytes};
			http_request.trusted_ca_pem_for_tests = config.trusted_ca_certificate_pem_for_tests;
			auto exchange = bhp::http_runtime::perform(
			    *runtime, http_request, bhp::http_runtime::CancellationToken::linked(local, global));
			const long http = exchange.has_value() ? exchange.value().status : 0;
			logger.log(
			    bhp::StructuredLogLevel::info, "lexinet_http_call", trace,
			    {{"target", "configured_lexinet_study_endpoint"},
			     {"method", "POST"},
			     {"duration_ms",
			      std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
						 Clock::now() - started)
						 .count())},
			     {"status", std::to_string(http)},
			     {"attempt", std::to_string(attempt + 1)}});
			if (local.cancelled() || global.cancelled())
				return Result::failure(cancelled());
			if (!exchange.has_value()) {
				const auto &error = exchange.error();
				if (error.code == "HTTP.CANCELLED")
					return Result::failure(cancelled());
				if (error.code == "HTTP.RESPONSE_TOO_LARGE")
					return Result::failure(
					    {"LEXINET.RESPONSE_SIZE_LIMIT_EXCEEDED",
					     "Lexinet response exceeded configured byte ceiling",
					     {},
					     false});
				if (error.code == "HTTP.TLS_VERIFICATION_FAILED")
					return Result::failure(
					    {"LEXINET.TLS_CERTIFICATE_VERIFICATION_FAILED",
					     "Lexinet TLS certificate or hostname verification failed",
					     {},
					     false});
				if (error.code == "HTTP.REQUEST_INVALID" || error.code.rfind("HTTP_RUNTIME.", 0) == 0)
					return Result::failure({error.code, error.message, {}, false});
				if (error.retryable && attempt < 2) {
					if (!wait_before_retry(attempt, deadline, local, global))
						return Result::failure(cancelled());
					continue;
				}
				return Result::failure(
				    {error.code == "HTTP.TIMEOUT" ? "LEXINET.TIMEOUT"
								 : "LEXINET.HTTP_TRANSPORT_FAILED",
				     "Lexinet transport failed",
				     {{"curl_code", std::to_string(error.transport_code)}},
				     error.retryable});
			}
			if ((http == 429 || http >= 500) && attempt < 2) {
				if (!wait_before_retry(attempt, deadline, local, global))
					return Result::failure(cancelled());
				continue;
			}
			const auto &response = exchange.value();
			const std::string &content_type = response.content_type;
			if (http >= 300 && http < 400)
				return Result::failure({"LEXINET.HTTP_STATUS_REJECTED",
							"Lexinet redirect refused",
							{{"status", std::to_string(http)}},
							false});
			if (content_type.rfind("application/json", 0) != 0)
				return Result::failure({"LEXINET.CONTENT_TYPE_INVALID",
							"Lexinet response is not JSON",
							{},
							false});
			if (http < 200 || http >= 300)
				return Result::failure(decode_study_error(response.body, http));
			return Result::success(response.body);
		}
		return Result::failure(
		    {"LEXINET.TIMEOUT", "Lexinet whole-request budget exhausted", {}, true});
	}
	std::optional<VocabularyPanelError> freshness(const StudySession &s)
	{
		if (!config.clock_trusted)
			return std::nullopt;
		auto parsed = study_timestamp_seconds(s.generated_at);
		if (!parsed.has_value())
			return parsed.error();
		const auto now =
		    config.current_utc_seconds +
		    std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - clock_origin)
			.count();
		const auto delta = now - parsed.value();
		if ((delta >= 0 &&
		     static_cast<std::uint64_t>(delta) > config.maximum_snapshot_age_seconds) ||
		    (delta < 0 &&
		     static_cast<std::uint64_t>(-delta) > config.allowed_clock_skew_seconds))
			return VocabularyPanelError{
			    "LEXINET.SNAPSHOT_STALE",
			    "Lexinet snapshot is outside configured freshness bounds",
			    {},
			    false};
		return std::nullopt;
	}
};
HttpLexinetClient::HttpLexinetClient(std::shared_ptr<bhp::http_runtime::HttpRuntime> r,
				     HttpLexinetConfig c, bhp::StructuredLogger &l)
    : implementation_(std::make_unique<Implementation>(std::move(r), std::move(c), l))
{
}
HttpLexinetClient::~HttpLexinetClient() = default;
VocabularyPanelResult<StudySession>
HttpLexinetClient::create_or_resume(const StudyCreateRequest &request, const std::string &trace,
				    bhp::http_runtime::CancellationToken cancel)
{
	auto wire = encode_study_create(request);
	if (!wire.has_value())
		return VocabularyPanelResult<StudySession>::failure(wire.error());
	auto response =
	    implementation_->request("/api/v1/study/sessions", wire.value(), trace, cancel);
	if (!response.has_value())
		return VocabularyPanelResult<StudySession>::failure(response.error());
	auto parsed = decode_study_session(response.value());
	if (cancel.cancelled() || implementation_->runtime->cancellation().cancelled())
		return VocabularyPanelResult<StudySession>::failure(cancelled());
	if (parsed.has_value()) {
		if (parsed.value().profile_ref != request.profile_ref)
			return VocabularyPanelResult<StudySession>::failure(
			    {"LEXINET.PROFILE_MISMATCH",
			     "Session response profile differs from request",
			     {},
			     false});
		if (auto e = implementation_->freshness(parsed.value()))
			return VocabularyPanelResult<StudySession>::failure(*e);
	}
	return parsed;
}
VocabularyPanelResult<StudySyncResponse>
HttpLexinetClient::sync(const StudySyncRequest &request, const std::string &trace,
			bhp::http_runtime::CancellationToken cancel)
{
	auto wire = encode_study_sync(request);
	if (!wire.has_value())
		return VocabularyPanelResult<StudySyncResponse>::failure(wire.error());
	auto response = implementation_->request(
	    "/api/v1/study/sessions/" + request.session_id + "/sync", wire.value(), trace, cancel);
	if (!response.has_value())
		return VocabularyPanelResult<StudySyncResponse>::failure(response.error());
	auto parsed = decode_study_sync(response.value());
	if (cancel.cancelled() || implementation_->runtime->cancellation().cancelled())
		return VocabularyPanelResult<StudySyncResponse>::failure(cancelled());
	if (parsed.has_value()) {
		if (parsed.value().session.session_id != request.session_id ||
		    parsed.value().sync_id != request.sync_id ||
		    parsed.value().client_stream_id != request.client_stream_id)
			return VocabularyPanelResult<StudySyncResponse>::failure(
			    {"LEXINET.SYNC_IDENTITY_MISMATCH",
			     "Sync response does not identify the submitted transaction",
			     {},
			     false});
		if (auto e = implementation_->freshness(parsed.value().session))
			return VocabularyPanelResult<StudySyncResponse>::failure(*e);
	}
	return parsed;
}
} // namespace vocabulary_panel
