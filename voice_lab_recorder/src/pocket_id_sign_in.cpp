#include "voice_lab_recorder/pocket_id_sign_in.h"
#include <cctype>
#include <nlohmann/json.hpp>
namespace voice_lab_recorder
{
namespace
{
using Json = nlohmann::json;
using bhp::http_runtime::HttpMethod;
using bhp::http_runtime::HttpRequest;
constexpr const char *kDeviceGrant = "urn:ietf:params:oauth:grant-type:device_code";
constexpr const char *kFormType = "application/x-www-form-urlencoded";

Error error(const char *code, const char *message, bool retryable = false)
{
	return {code, message, {}, retryable};
}

/** Maps a transport failure; the message never carries a URL, a body or a token. */
Error transport_error(const bhp::http_runtime::Error &failure)
{
	return {"VOICE_LAB.POCKET_ID_UNREACHABLE", "Pocket ID did not answer", {{"http_code", failure.code}},
		failure.retryable};
}

/** The scheme and authority of an https URL, or empty. */
std::string https_origin(const std::string &url)
{
	if (url.rfind("https://", 0) != 0)
		return {};
	const auto slash = url.find('/', 8);
	const auto origin = url.substr(0, slash);
	if (origin.size() <= 8 || origin.find_first_of("@?# ") != std::string::npos)
		return {};
	return origin;
}

HttpRequest form_post(const SignInSettings &settings, const std::string &url, const std::string &body)
{
	HttpRequest request;
	request.method = HttpMethod::Post;
	request.url = url;
	request.headers = {{"Accept", "application/json"}, {"Content-Type", kFormType}};
	request.body = body;
	request.budget = settings.budget;
	request.trusted_ca_pem_for_tests = settings.trusted_ca_pem_for_tests;
	return request;
}

std::optional<Json> json_body(const bhp::http_runtime::HttpResponse &response)
{
	if (response.content_type.rfind("application/json", 0) != 0)
		return std::nullopt;
	Json value = Json::parse(response.body, nullptr, false);
	if (value.is_discarded() || !value.is_object())
		return std::nullopt;
	return value;
}

std::string text(const Json &object, const char *key)
{
	return object.contains(key) && object.at(key).is_string() ? object.at(key).get<std::string>() : std::string();
}

/** Reads a token response into a pair; empty on any missing or malformed field. */
std::optional<TokenPair> token_pair(const Json &body, std::chrono::steady_clock::time_point now)
{
	const auto access = text(body, "access_token");
	const auto refresh = text(body, "refresh_token");
	if (access.empty() || refresh.empty() || !body.contains("expires_in") || !body.at("expires_in").is_number_integer())
		return std::nullopt;
	const auto lifetime = body.at("expires_in").get<long long>();
	if (lifetime <= 0 || lifetime > 86400)
		return std::nullopt;
	return TokenPair{access, refresh, now + std::chrono::seconds(lifetime)};
}
} // namespace

std::string form_encode(const std::string &value)
{
	static const char *hex = "0123456789ABCDEF";
	std::string out;
	for (const unsigned char c : value) {
		if (std::isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~')
			out += static_cast<char>(c);
		else {
			out += '%';
			out += hex[c >> 4];
			out += hex[c & 15];
		}
	}
	return out;
}

Result<Endpoints> discover(const SignInSettings &settings, const HttpTransport &transport)
{
	const auto origin = https_origin(settings.issuer);
	if (origin.empty() || origin != settings.issuer)
		return Result<Endpoints>::failure(error("VOICE_LAB.ISSUER_INVALID", "The issuer must be https://HOST"));
	HttpRequest request;
	request.method = HttpMethod::Get;
	request.url = settings.issuer + "/.well-known/openid-configuration";
	request.headers = {{"Accept", "application/json"}};
	request.budget = settings.budget;
	request.trusted_ca_pem_for_tests = settings.trusted_ca_pem_for_tests;
	auto response = transport(request);
	if (!response.has_value())
		return Result<Endpoints>::failure(transport_error(response.error()));
	const auto body = json_body(response.value());
	if (response.value().status != 200 || !body)
		return Result<Endpoints>::failure(error("VOICE_LAB.DISCOVERY_INVALID", "Discovery document unreadable", true));
	Endpoints endpoints{text(*body, "device_authorization_endpoint"), text(*body, "token_endpoint")};
	bool device_grant = false;
	if (body->contains("grant_types_supported") && body->at("grant_types_supported").is_array())
		for (const auto &grant : body->at("grant_types_supported"))
			device_grant = device_grant || (grant.is_string() && grant.get<std::string>() == kDeviceGrant);
	if (text(*body, "issuer") != settings.issuer || !device_grant ||
	    https_origin(endpoints.device_authorization) != origin || https_origin(endpoints.token) != origin)
		return Result<Endpoints>::failure(error("VOICE_LAB.DISCOVERY_INVALID",
							"Discovery must name this issuer, the device grant and same-origin endpoints"));
	return Result<Endpoints>::success(endpoints);
}

Result<DeviceCodeSignIn> DeviceCodeSignIn::start(const SignInSettings &settings, const Endpoints &endpoints,
						 HttpTransport transport, Clock clock)
{
	using Output = Result<DeviceCodeSignIn>;
	std::string body = "client_id=" + form_encode(settings.client_id);
	if (!settings.scope.empty())
		body += "&scope=" + form_encode(settings.scope);
	auto response = transport(form_post(settings, endpoints.device_authorization, body));
	if (!response.has_value())
		return Output::failure(transport_error(response.error()));
	const auto json = json_body(response.value());
	if (response.value().status != 200 || !json)
		return Output::failure(error("VOICE_LAB.DEVICE_CODE_REFUSED", "Pocket ID refused the device code request",
					     response.value().status >= 500));
	const auto device_code = text(*json, "device_code");
	DevicePrompt prompt;
	prompt.verification_uri = text(*json, "verification_uri");
	prompt.user_code = text(*json, "user_code");
	const bool lifetime = json->contains("expires_in") && json->at("expires_in").is_number_integer() &&
			      json->at("expires_in").get<long long>() > 0 && json->at("expires_in").get<long long>() <= 3600;
	if (device_code.empty() || prompt.user_code.empty() || prompt.user_code.size() > 32 || !lifetime ||
	    https_origin(prompt.verification_uri) != https_origin(settings.issuer))
		return Output::failure(error("VOICE_LAB.DEVICE_CODE_INVALID", "Device code response is incomplete"));
	const auto now = clock();
	prompt.expires_at = now + std::chrono::seconds(json->at("expires_in").get<long long>());
	if (json->contains("interval") && json->at("interval").is_number_integer()) {
		const auto interval = json->at("interval").get<long long>();
		if (interval < 1 || interval > 60)
			return Output::failure(error("VOICE_LAB.DEVICE_CODE_INVALID", "Polling interval out of range"));
		prompt.interval = std::chrono::seconds(interval);
	}
	DeviceCodeSignIn sign_in(settings, endpoints, std::move(transport), std::move(clock), prompt, device_code);
	sign_in.next_poll_ = now + prompt.interval;
	return Output::success(std::move(sign_in));
}

Result<DeviceCodeSignIn::Poll> DeviceCodeSignIn::poll()
{
	using Output = Result<Poll>;
	if (finished_)
		return Output::failure(error("VOICE_LAB.SIGN_IN_FINISHED", "This sign-in has already ended"));
	const auto now = clock_();
	if (now >= prompt_.expires_at) {
		finished_ = true;
		return Output::success({State::Expired, std::nullopt});
	}
	if (now < next_poll_)
		return Output::success({State::Pending, std::nullopt});
	next_poll_ = now + prompt_.interval;
	const auto body = "grant_type=" + form_encode(kDeviceGrant) + "&device_code=" + form_encode(device_code_) +
			  "&client_id=" + form_encode(settings_.client_id);
	auto response = transport_(form_post(settings_, endpoints_.token, body));
	if (!response.has_value())
		return Output::failure(transport_error(response.error()));
	const auto json = json_body(response.value());
	if (!json)
		return Output::failure(error("VOICE_LAB.TOKEN_RESPONSE_INVALID", "Token response is not JSON", true));
	if (response.value().status == 200) {
		auto tokens = token_pair(*json, clock_());
		if (!tokens)
			return Output::failure(error("VOICE_LAB.TOKEN_RESPONSE_INVALID", "Token response is incomplete"));
		finished_ = true;
		return Output::success({State::Approved, std::move(tokens)});
	}
	const auto code = text(*json, "error");
	if (code == "authorization_pending")
		return Output::success({State::Pending, std::nullopt});
	if (code == "slow_down") {
		prompt_.interval += std::chrono::seconds(5);
		next_poll_ = now + prompt_.interval;
		return Output::success({State::Pending, std::nullopt});
	}
	finished_ = true;
	if (code == "expired_token")
		return Output::success({State::Expired, std::nullopt});
	return Output::failure({"VOICE_LAB.SIGN_IN_REFUSED", "Pocket ID refused the sign-in", {{"oauth_error", code}}, false});
}

RefreshTokenStore::RefreshTokenStore(std::string directory)
	: file_(std::move(directory), {"refresh-token.lock", "refresh-token.json", "refresh-token.tmp", "VOICE_LAB"}, 16384)
{
}

Result<std::optional<std::string>> RefreshTokenStore::load(const std::string &issuer, const std::string &client_id)
{
	using Output = Result<std::optional<std::string>>;
	auto read = file_.read();
	if (!read.has_value())
		return Output::failure({read.error().code, read.error().message, read.error().context, false});
	if (!read.value())
		return Output::success(std::nullopt);
	const auto json = Json::parse(*read.value(), nullptr, false);
	if (json.is_discarded() || !json.is_object() || json.value("schema_version", 0) != 1)
		return Output::failure(error("VOICE_LAB.STORAGE_INVALID", "Refresh token file is not a version 1 record"));
	if (json.value("signed_out", false))
		return Output::success(std::nullopt);
	if (text(json, "issuer") != issuer || text(json, "client_id") != client_id || text(json, "refresh_token").empty())
		return Output::failure(error("VOICE_LAB.STORAGE_MISMATCH", "Saved token belongs to another issuer or client"));
	return Output::success(text(json, "refresh_token"));
}

Result<bool> RefreshTokenStore::save(const std::string &issuer, const std::string &client_id,
				     const std::string &refresh_token)
{
	const Json record = {{"schema_version", 1}, {"issuer", issuer}, {"client_id", client_id},
			     {"refresh_token", refresh_token}, {"signed_out", false}};
	auto committed = file_.commit(record.dump() + "\n");
	if (!committed.has_value())
		return Result<bool>::failure({committed.error().code, committed.error().message, committed.error().context, false});
	return Result<bool>::success(true);
}

Result<bool> RefreshTokenStore::clear()
{
	auto committed = file_.commit(Json({{"schema_version", 1}, {"signed_out", true}}).dump() + "\n");
	if (!committed.has_value())
		return Result<bool>::failure({committed.error().code, committed.error().message, committed.error().context, false});
	return Result<bool>::success(true);
}

VoiceLabSession::VoiceLabSession(SignInSettings settings, Endpoints endpoints, HttpTransport transport,
				 RefreshTokenStore &store, Clock clock, std::chrono::seconds refresh_margin)
	: settings_(std::move(settings)), endpoints_(std::move(endpoints)), transport_(std::move(transport)),
	  store_(store), clock_(std::move(clock)), refresh_margin_(refresh_margin)
{
}

Result<bool> VoiceLabSession::restore()
{
	std::lock_guard<std::mutex> lock(mutex_);
	auto loaded = store_.load(settings_.issuer, settings_.client_id);
	if (!loaded.has_value())
		return Result<bool>::failure(loaded.error());
	refresh_token_ = loaded.value();
	access_token_.reset();
	return Result<bool>::success(refresh_token_.has_value());
}

Result<bool> VoiceLabSession::adopt(const TokenPair &tokens)
{
	std::lock_guard<std::mutex> lock(mutex_);
	auto saved = store_.save(settings_.issuer, settings_.client_id, tokens.refresh_token);
	if (!saved.has_value()) {
		sign_out_locked();
		return Result<bool>::failure(saved.error());
	}
	refresh_token_ = tokens.refresh_token;
	access_token_ = tokens.access_token;
	access_expires_at_ = tokens.access_expires_at;
	return Result<bool>::success(true);
}

bool VoiceLabSession::signed_in() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return refresh_token_.has_value();
}

unsigned VoiceLabSession::refreshes() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return refreshes_;
}

void VoiceLabSession::sign_out_locked()
{
	refresh_token_.reset();
	access_token_.reset();
}

Result<std::string> VoiceLabSession::access_token()
{
	// Holding the lock across the exchange is the single flight: a second caller
	// waits here and then finds the fresh access token.
	std::lock_guard<std::mutex> lock(mutex_);
	if (!refresh_token_)
		return Result<std::string>::failure(error("VOICE_LAB.SIGN_IN_REQUIRED", "The panel must sign in to voice-lab"));
	if (access_token_ && clock_() + refresh_margin_ < access_expires_at_)
		return Result<std::string>::success(*access_token_);
	return refresh_locked();
}

Result<std::string> VoiceLabSession::refresh_locked()
{
	using Output = Result<std::string>;
	const auto body = "grant_type=refresh_token&refresh_token=" + form_encode(*refresh_token_) +
			  "&client_id=" + form_encode(settings_.client_id);
	auto response = transport_(form_post(settings_, endpoints_.token, body));
	if (!response.has_value())
		return Output::failure(transport_error(response.error()));
	const auto json = json_body(response.value());
	if (response.value().status != 200) {
		if (json && text(*json, "error") == "invalid_grant") {
			// The family is revoked (or the token rotated away): sign in again.
			sign_out_locked();
			(void)store_.clear();
			return Output::failure(error("VOICE_LAB.SIGN_IN_REQUIRED", "The refresh token is no longer valid"));
		}
		return Output::failure(error("VOICE_LAB.REFRESH_FAILED", "Pocket ID refused the refresh",
					     response.value().status >= 500 || response.value().status == 429));
	}
	auto tokens = json ? token_pair(*json, clock_()) : std::nullopt;
	if (!tokens) {
		// A 200 without a usable pair may still have rotated the token server side.
		sign_out_locked();
		return Output::failure(error("VOICE_LAB.TOKEN_RESPONSE_INVALID", "Refresh response is incomplete"));
	}
	auto saved = store_.save(settings_.issuer, settings_.client_id, tokens->refresh_token);
	if (!saved.has_value()) {
		// The token on disk was rotated away; using the new access token would leave
		// no way to refresh again.
		sign_out_locked();
		return Output::failure(error("VOICE_LAB.SIGN_IN_REQUIRED", "The rotated refresh token could not be saved"));
	}
	++refreshes_;
	refresh_token_ = tokens->refresh_token;
	access_token_ = tokens->access_token;
	access_expires_at_ = tokens->access_expires_at;
	return Output::success(*access_token_);
}
} // namespace voice_lab_recorder
