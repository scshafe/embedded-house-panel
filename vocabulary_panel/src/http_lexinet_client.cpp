#include "vocabulary_panel/http_lexinet_client.h"

#include "bhp/structured_logger.h"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace {

using Json = nlohmann::json;
constexpr std::size_t RESPONSE_BODY_LIMIT_DEFAULT = 262144;

struct ResponseBuffer {
	std::string response_body;
	std::size_t maximum_response_bytes{RESPONSE_BODY_LIMIT_DEFAULT};
};

std::size_t append_response(char *contents, std::size_t size, std::size_t count, void *user_data)
{
	const std::size_t byte_count = size * count;
	auto &buffer = *static_cast<ResponseBuffer *>(user_data);
	if (buffer.response_body.size() + byte_count > buffer.maximum_response_bytes)
		return 0;
	buffer.response_body.append(contents, byte_count);
	return byte_count;
}

std::string read_credential_file(const std::string &credential_file_path)
{
	if (credential_file_path.empty())
		return {};
	std::ifstream credential_file(credential_file_path);
	if (!credential_file)
		throw std::runtime_error("LEXINET.CREDENTIAL_FILE_OPEN_FAILED");
	std::string credential;
	std::getline(credential_file, credential);
	if (credential.empty())
		throw std::runtime_error("LEXINET.CREDENTIAL_FILE_EMPTY");
	return credential;
}

std::string review_rating_json_name(const vocabulary_panel::ReviewRating rating)
{
	return vocabulary_panel::review_rating_name(rating);
}

} // namespace

namespace vocabulary_panel {

struct HttpLexinetClient::Implementation {
	explicit Implementation(HttpLexinetConfig initial_config, bhp::StructuredLogger &initial_logger)
		: config(std::move(initial_config)), logger(initial_logger),
		  credential(read_credential_file(config.credential_file_path))
	{
		if (config.lexinet_base_url.empty() || config.next_card_path.empty() ||
		    config.review_path.empty() || config.request_timeout_ms <= 0 ||
		    config.maximum_response_bytes == 0) {
			throw std::invalid_argument("LEXINET.CONFIG_INVALID");
		}
		if (!config.allow_insecure_http_for_tests &&
		    config.lexinet_base_url.rfind("https://", 0) != 0) {
			throw std::invalid_argument("LEXINET.HTTPS_REQUIRED");
		}
		if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
			throw std::runtime_error("LEXINET.CURL_GLOBAL_INIT_FAILED");
	}

	~Implementation() { curl_global_cleanup(); }

	VocabularyPanelResult<Json> request(const std::string &method,
					    const std::string &url,
					    const std::string &request_body,
					    const std::string &idempotency_key,
					    const std::string &trace_id)
	{
		CURL *curl = curl_easy_init();
		if (curl == nullptr) {
			return VocabularyPanelResult<Json>::failure({
				"LEXINET.CURL_INIT_FAILED", "Failed to initialize HTTP request",
				{{"method", method}, {"target", config.lexinet_base_url}}, true});
		}

		ResponseBuffer response{{}, config.maximum_response_bytes};
		curl_slist *headers = nullptr;
		headers = curl_slist_append(headers, "Accept: application/json");
		headers = curl_slist_append(headers, ("X-Request-Id: " + trace_id).c_str());
		if (!credential.empty())
			headers = curl_slist_append(headers, ("Authorization: Bearer " + credential).c_str());
		if (method == "POST") {
			headers = curl_slist_append(headers, "Content-Type: application/json");
			headers = curl_slist_append(headers, ("Idempotency-Key: " + idempotency_key).c_str());
			curl_easy_setopt(curl, CURLOPT_POST, 1L);
			curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_body.c_str());
			curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE,
					 static_cast<long>(request_body.size()));
		}
		curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
		curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, config.request_timeout_ms);
		curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, config.request_timeout_ms);
		curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, append_response);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
		curl_easy_setopt(curl, CURLOPT_USERAGENT, "beaglebone-house-panel/0.1");

		const auto started_at = std::chrono::steady_clock::now();
		const CURLcode curl_result = curl_easy_perform(curl);
		long status_code = 0;
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);
		const auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now() - started_at).count();
		curl_slist_free_all(headers);
		curl_easy_cleanup(curl);

		logger.log(bhp::StructuredLogLevel::info, "lexinet_http_call", trace_id,
			   {{"target", config.lexinet_base_url},
			    {"method", method},
			    {"duration_ms", std::to_string(duration_ms)},
			    {"status", std::to_string(status_code)}});

		if (curl_result != CURLE_OK) {
			return VocabularyPanelResult<Json>::failure({
				"LEXINET.HTTP_TRANSPORT_FAILED", curl_easy_strerror(curl_result),
				{{"method", method}, {"target", config.lexinet_base_url}}, true});
		}
		if (status_code < 200 || status_code >= 300) {
			return VocabularyPanelResult<Json>::failure({
				"LEXINET.HTTP_STATUS_REJECTED", "Lexinet returned a non-success status",
				{{"method", method},
				 {"target", config.lexinet_base_url},
				 {"status", std::to_string(status_code)}},
				status_code == 429 || status_code >= 500});
		}
		try {
			return VocabularyPanelResult<Json>::success(Json::parse(response.response_body));
		} catch (const Json::exception &error) {
			return VocabularyPanelResult<Json>::failure({
				"LEXINET.RESPONSE_JSON_INVALID", error.what(),
				{{"method", method}, {"target", config.lexinet_base_url}}, false});
		}
	}

	HttpLexinetConfig config;
	bhp::StructuredLogger &logger;
	std::string credential;
};

HttpLexinetClient::HttpLexinetClient(HttpLexinetConfig config, bhp::StructuredLogger &logger)
	: implementation_(std::make_unique<Implementation>(std::move(config), logger))
{
}

HttpLexinetClient::~HttpLexinetClient() = default;

VocabularyPanelResult<std::optional<FlashCard>>
HttpLexinetClient::fetch_next_card(const std::string &review_cursor,
				   const std::string &trace_id)
{
	std::string url = implementation_->config.lexinet_base_url +
			  implementation_->config.next_card_path;
	if (!review_cursor.empty()) {
		CURL *escape_handle = curl_easy_init();
		if (escape_handle == nullptr)
			return VocabularyPanelResult<std::optional<FlashCard>>::failure(
				{"LEXINET.CURL_INIT_FAILED", "Failed to encode review cursor", {}, true});
		char *escaped = curl_easy_escape(escape_handle, review_cursor.c_str(),
					 static_cast<int>(review_cursor.size()));
		if (escaped == nullptr) {
			curl_easy_cleanup(escape_handle);
			return VocabularyPanelResult<std::optional<FlashCard>>::failure(
				{"LEXINET.CURSOR_ENCODING_FAILED", "Failed to encode review cursor", {}, false});
		}
		url += "?cursor=" + std::string(escaped);
		curl_free(escaped);
		curl_easy_cleanup(escape_handle);
	}

	const auto response = implementation_->request("GET", url, "", "", trace_id);
	if (!response.has_value())
		return VocabularyPanelResult<std::optional<FlashCard>>::failure(response.error());
	try {
		const Json &envelope = response.value();
		if (!envelope.contains("error") || !envelope.at("error").is_null())
			return VocabularyPanelResult<std::optional<FlashCard>>::failure(
				{"LEXINET.RESPONSE_ERROR_PRESENT", "Lexinet response contains an error", {}, false});
		if (!envelope.contains("data"))
			return VocabularyPanelResult<std::optional<FlashCard>>::failure({
				"LEXINET.RESPONSE_SCHEMA_INVALID",
				"Lexinet success response is missing the required data member",
				{{"operation", "fetch_next_card"}}, false});
		if (envelope.at("data").is_null())
			return VocabularyPanelResult<std::optional<FlashCard>>::success(std::nullopt);
		const Json &data = envelope.at("data");
		FlashCard card{
			data.at("vocabulary_card_id").get<std::string>(),
			data.at("word").get<std::string>(),
			data.at("definition").get<std::string>(),
			data.at("example_sentence").get<std::string>(),
			data.value("review_cursor", ""),
		};
		return VocabularyPanelResult<std::optional<FlashCard>>::success(std::move(card));
	} catch (const Json::exception &error) {
		return VocabularyPanelResult<std::optional<FlashCard>>::failure({
			"LEXINET.RESPONSE_SCHEMA_INVALID", error.what(),
			{{"operation", "fetch_next_card"}}, false});
	}
}

VocabularyPanelResult<ReviewReceipt>
HttpLexinetClient::submit_review(const ReviewCommand &review_command,
				 const std::string &trace_id)
{
	const Json request_body = {
		{"vocabulary_card_id", review_command.vocabulary_card_id},
		{"review_rating", review_rating_json_name(review_command.review_rating)},
		{"reviewed_at", review_command.reviewed_at_utc},
	};
	const auto response = implementation_->request(
		"POST", implementation_->config.lexinet_base_url + implementation_->config.review_path,
		request_body.dump(), review_command.idempotency_key, trace_id);
	if (!response.has_value())
		return VocabularyPanelResult<ReviewReceipt>::failure(response.error());
	try {
		const Json &envelope = response.value();
		if (!envelope.contains("error") || !envelope.at("error").is_null())
			return VocabularyPanelResult<ReviewReceipt>::failure(
				{"LEXINET.RESPONSE_ERROR_PRESENT", "Lexinet response contains an error", {}, false});
		const Json &data = envelope.at("data");
		return VocabularyPanelResult<ReviewReceipt>::success({
			data.at("vocabulary_review_result_id").get<std::string>(),
			data.at("vocabulary_card_id").get<std::string>(),
			data.at("saved_at").get<std::string>(),
		});
	} catch (const Json::exception &error) {
		return VocabularyPanelResult<ReviewReceipt>::failure({
			"LEXINET.RESPONSE_SCHEMA_INVALID", error.what(),
			{{"operation", "submit_review"}}, false});
	}
}

} // namespace vocabulary_panel
