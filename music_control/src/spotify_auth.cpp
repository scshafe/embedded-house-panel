#include "music_control/spotify_auth.h"
#include <sys/stat.h>
#include <algorithm>
#include <cctype>
#include <nlohmann/json.hpp>
#include <openssl/evp.h>
#include <set>
#include <sstream>
#include <vector>
namespace music_control
{
namespace
{
using Json = nlohmann::json;
using bhp::http_runtime::HttpMethod;
using bhp::http_runtime::HttpRequest;
constexpr const char *kFormType = "application/x-www-form-urlencoded";

Error error(const char *code, const char *message, bool retryable = false)
{
	return {code, message, {}, retryable};
}

/** Maps a transport failure; the message never carries a URL, a body or a token. */
Error transport_error(const bhp::http_runtime::Error &failure)
{
	return {"MUSIC.SPOTIFY_UNREACHABLE", "Spotify did not answer", {{"http_code", failure.code}}, failure.retryable};
}

std::string base64url(const unsigned char *bytes, std::size_t size)
{
	static const char *alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
	std::string out;
	std::size_t at = 0;
	for (; at + 2 < size; at += 3) {
		const unsigned value = (bytes[at] << 16) | (bytes[at + 1] << 8) | bytes[at + 2];
		for (int shift = 18; shift >= 0; shift -= 6)
			out += alphabet[(value >> shift) & 63];
	}
	if (size - at == 1) {
		const unsigned value = bytes[at] << 16;
		out += alphabet[(value >> 18) & 63];
		out += alphabet[(value >> 12) & 63];
	} else if (size - at == 2) {
		const unsigned value = (bytes[at] << 16) | (bytes[at + 1] << 8);
		out += alphabet[(value >> 18) & 63];
		out += alphabet[(value >> 12) & 63];
		out += alphabet[(value >> 6) & 63];
	}
	return out;
}

HttpRequest form_post(const SpotifyAuthSettings &settings, const std::string &body)
{
	HttpRequest request;
	request.method = HttpMethod::Post;
	request.url = settings.token_url;
	request.headers = {{"Accept", "application/json"}, {"Content-Type", kFormType}};
	request.body = body;
	request.budget = settings.budget;
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

/** Reads a token response; empty on a missing or malformed access token or lifetime. */
std::optional<TokenPair> token_pair(const Json &body, std::chrono::steady_clock::time_point now)
{
	const auto access = text(body, "access_token");
	if (access.empty() || !body.contains("expires_in") || !body.at("expires_in").is_number_integer())
		return std::nullopt;
	const auto lifetime = body.at("expires_in").get<long long>();
	if (lifetime <= 0 || lifetime > 86400)
		return std::nullopt;
	TokenPair pair{access, std::nullopt, now + std::chrono::seconds(lifetime), text(body, "scope")};
	if (body.contains("refresh_token")) {
		if (!body.at("refresh_token").is_string() || body.at("refresh_token").get<std::string>().empty())
			return std::nullopt;
		pair.refresh_token = body.at("refresh_token").get<std::string>();
	}
	return pair;
}

bool grants_playback_scopes(const std::string &scope)
{
	std::set<std::string> granted;
	std::istringstream words(scope);
	for (std::string word; words >> word;)
		granted.insert(word);
	return granted.count("user-read-playback-state") && granted.count("user-modify-playback-state");
}

/** The value of one query parameter, percent-decoded; nullopt when absent or malformed. */
std::optional<std::string> query_value(const std::string &query, const std::string &key)
{
	std::size_t at = 0;
	while (at <= query.size()) {
		const auto end = std::min(query.find('&', at), query.size());
		const auto pair = query.substr(at, end - at);
		const auto equals = pair.find('=');
		if (pair.substr(0, equals) == key) {
			if (equals == std::string::npos)
				return std::string();
			std::string decoded;
			const auto raw = pair.substr(equals + 1);
			for (std::size_t i = 0; i < raw.size(); ++i) {
				if (raw[i] == '+')
					decoded += ' ';
				else if (raw[i] == '%') {
					if (i + 2 >= raw.size() || !std::isxdigit(static_cast<unsigned char>(raw[i + 1])) ||
					    !std::isxdigit(static_cast<unsigned char>(raw[i + 2])))
						return std::nullopt;
					decoded += static_cast<char>(std::stoi(raw.substr(i + 1, 2), nullptr, 16));
					i += 2;
				} else
					decoded += raw[i];
			}
			return decoded;
		}
		at = end + 1;
	}
	return std::nullopt;
}

bool token_characters(const std::string &value, std::size_t maximum)
{
	return !value.empty() && value.size() <= maximum && std::all_of(value.begin(), value.end(), [](unsigned char c) {
		return std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~';
	});
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

bool valid_client_id(const std::string &client_id)
{
	return client_id.size() >= 16 && client_id.size() <= 64 &&
	       std::all_of(client_id.begin(), client_id.end(), [](unsigned char c) { return std::isalnum(c); });
}

bool valid_loopback_redirect(const std::string &redirect_uri)
{
	const std::string prefix = "http://127.0.0.1:", suffix = "/callback";
	if (redirect_uri.size() <= prefix.size() + suffix.size() || redirect_uri.rfind(prefix, 0) != 0 ||
	    redirect_uri.compare(redirect_uri.size() - suffix.size(), suffix.size(), suffix) != 0)
		return false;
	const auto port = redirect_uri.substr(prefix.size(), redirect_uri.size() - prefix.size() - suffix.size());
	if (port.empty() || port.size() > 5 || port[0] == '0' ||
	    !std::all_of(port.begin(), port.end(), [](unsigned char c) { return std::isdigit(c); }))
		return false;
	const auto number = std::stoul(port);
	return number >= 1024 && number <= 65535;
}

std::string code_challenge(const std::string &verifier)
{
	unsigned char digest[EVP_MAX_MD_SIZE];
	unsigned size = 0;
	EVP_Digest(verifier.data(), verifier.size(), digest, &size, EVP_sha256(), nullptr);
	return base64url(digest, size);
}

Result<PkceStart> start_pkce(const SpotifyAuthSettings &settings, const std::string &redirect_uri,
			     const RandomBytes &random)
{
	using Output = Result<PkceStart>;
	if (!valid_client_id(settings.client_id) || !valid_loopback_redirect(redirect_uri))
		return Output::failure(error("MUSIC.SIGN_IN_INVALID", "The client ID or the redirect URI is invalid"));
	unsigned char bytes[72];
	if (!random || !random(bytes, sizeof bytes))
		return Output::failure(error("MUSIC.RANDOM_UNAVAILABLE", "No randomness for the sign-in"));
	PkceStart start;
	// 48 bytes give 64 base64url characters (RFC 7636: 43 to 128); 24 more give the state.
	start.verifier = base64url(bytes, 48);
	start.state = base64url(bytes + 48, 24);
	start.authorize_url = settings.authorize_url + "?response_type=code&client_id=" + form_encode(settings.client_id) +
			      "&scope=" + form_encode(kSpotifyScopes) + "&redirect_uri=" + form_encode(redirect_uri) +
			      "&code_challenge_method=S256&code_challenge=" + code_challenge(start.verifier) +
			      "&state=" + start.state;
	return Output::success(start);
}

Result<std::string> authorization_code(const std::string &request_target, const std::string &expected_state)
{
	using Output = Result<std::string>;
	const std::string path = "/callback?";
	if (request_target.rfind(path, 0) != 0 || request_target.size() > 4096)
		return Output::failure(error("MUSIC.SIGN_IN_INVALID", "The redirect is not the callback"));
	const auto query = request_target.substr(path.size());
	const auto state = query_value(query, "state");
	if (!state || expected_state.empty() || *state != expected_state)
		return Output::failure(error("MUSIC.SIGN_IN_STATE_MISMATCH", "The redirect's state is not this sign-in's"));
	if (query_value(query, "error"))
		return Output::failure(error("MUSIC.SIGN_IN_DENIED", "Spotify did not grant access"));
	const auto code = query_value(query, "code");
	if (!code || !token_characters(*code, 2048))
		return Output::failure(error("MUSIC.SIGN_IN_INVALID", "The redirect carries no code"));
	return Output::success(*code);
}

Result<TokenPair> exchange_code(const SpotifyAuthSettings &settings, const std::string &redirect_uri,
				const std::string &code, const std::string &verifier, const HttpTransport &transport,
				const Clock &clock)
{
	using Output = Result<TokenPair>;
	if (!valid_client_id(settings.client_id) || !valid_loopback_redirect(redirect_uri))
		return Output::failure(error("MUSIC.SIGN_IN_INVALID", "The client ID or the redirect URI is invalid"));
	const auto body = "grant_type=authorization_code&code=" + form_encode(code) +
			  "&redirect_uri=" + form_encode(redirect_uri) + "&client_id=" + form_encode(settings.client_id) +
			  "&code_verifier=" + form_encode(verifier);
	auto response = transport(form_post(settings, body));
	if (!response.has_value())
		return Output::failure(transport_error(response.error()));
	const auto json = json_body(response.value());
	if (response.value().status != 200) {
		Error refused = error("MUSIC.SIGN_IN_REFUSED", "Spotify refused the sign-in", response.value().status >= 500);
		if (json && !text(*json, "error").empty() && text(*json, "error").size() <= 64)
			refused.context["oauth_error"] = text(*json, "error");
		return Output::failure(refused);
	}
	auto tokens = json ? token_pair(*json, clock()) : std::nullopt;
	if (!tokens || !tokens->refresh_token)
		return Output::failure(error("MUSIC.TOKEN_RESPONSE_INVALID", "The sign-in answer is incomplete"));
	if (!grants_playback_scopes(tokens->scope))
		return Output::failure(error("MUSIC.SIGN_IN_SCOPE_MISSING", "Spotify did not grant both playback scopes"));
	return Output::success(*tokens);
}

SpotifyTokenStore::SpotifyTokenStore(std::string directory)
	: path_(directory + "/spotify-token.json"),
	  file_(std::move(directory), {"spotify-token.lock", "spotify-token.json", "spotify-token.tmp", "MUSIC"}, 16384)
{
}

std::optional<SpotifyTokenStore::FileIdentity> SpotifyTokenStore::identity() const
{
	struct stat info {};
	if (::stat(path_.c_str(), &info) != 0)
		return std::nullopt;
	return FileIdentity{static_cast<unsigned long long>(info.st_dev), static_cast<unsigned long long>(info.st_ino),
			    static_cast<unsigned long long>(info.st_size),
			    static_cast<long long>(info.st_mtim.tv_sec) * 1000000000LL + info.st_mtim.tv_nsec};
}

void SpotifyTokenStore::remember()
{
	auto now = identity();
	std::lock_guard<std::mutex> lock(seen_mutex_);
	seen_ = now;
}

bool SpotifyTokenStore::changed_on_disk() const
{
	auto now = identity();
	std::lock_guard<std::mutex> lock(seen_mutex_);
	return !(now == seen_);
}

Result<std::optional<std::string>> SpotifyTokenStore::load(const std::string &client_id)
{
	using Output = Result<std::optional<std::string>>;
	// Before the read: a file replaced during it is then seen as changed again.
	remember();
	auto read = file_.read();
	if (!read.has_value())
		return Output::failure({read.error().code, read.error().message, read.error().context, false});
	if (!read.value())
		return Output::success(std::nullopt);
	const auto json = Json::parse(*read.value(), nullptr, false);
	if (json.is_discarded() || !json.is_object() || json.value("schema_version", 0) != 1)
		return Output::failure(error("MUSIC.STORAGE_INVALID", "The token file is not a version 1 record"));
	if (json.value("signed_out", false))
		return Output::success(std::nullopt);
	if (text(json, "client_id") != client_id || text(json, "refresh_token").empty())
		return Output::failure(error("MUSIC.STORAGE_MISMATCH", "The saved token belongs to another client"));
	return Output::success(text(json, "refresh_token"));
}

Result<bool> SpotifyTokenStore::save(const std::string &client_id, const std::string &refresh_token)
{
	const Json record = {{"schema_version", 1}, {"client_id", client_id}, {"refresh_token", refresh_token},
			     {"signed_out", false}};
	auto committed = file_.commit(record.dump() + "\n");
	remember();
	if (!committed.has_value())
		return Result<bool>::failure({committed.error().code, committed.error().message, committed.error().context, false});
	return Result<bool>::success(true);
}

Result<bool> SpotifyTokenStore::clear()
{
	auto committed = file_.commit(Json({{"schema_version", 1}, {"signed_out", true}}).dump() + "\n");
	remember();
	if (!committed.has_value())
		return Result<bool>::failure({committed.error().code, committed.error().message, committed.error().context, false});
	return Result<bool>::success(true);
}

SpotifySession::SpotifySession(SpotifyAuthSettings settings, HttpTransport transport, SpotifyTokenStore &store,
			       Clock clock, std::chrono::seconds refresh_margin)
	: settings_(std::move(settings)), transport_(std::move(transport)), store_(store), clock_(std::move(clock)),
	  refresh_margin_(refresh_margin)
{
}

Result<bool> SpotifySession::restore()
{
	std::lock_guard<std::mutex> lock(mutex_);
	auto loaded = store_.load(settings_.client_id);
	if (!loaded.has_value())
		return Result<bool>::failure(loaded.error());
	refresh_token_ = loaded.value();
	access_token_.reset();
	return Result<bool>::success(refresh_token_.has_value());
}

void SpotifySession::invalidate_access_token()
{
	std::lock_guard<std::mutex> lock(mutex_);
	access_token_.reset();
}

bool SpotifySession::signed_in() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return refresh_token_.has_value();
}

unsigned SpotifySession::refreshes() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return refreshes_;
}

void SpotifySession::sign_out_locked()
{
	refresh_token_.reset();
	access_token_.reset();
}

Result<std::string> SpotifySession::access_token()
{
	// Holding the lock across the exchange is the single flight: a second caller
	// waits here and then finds the fresh access token.
	std::lock_guard<std::mutex> lock(mutex_);
	if (!refresh_token_)
		return Result<std::string>::failure(error("MUSIC.SIGN_IN_REQUIRED", "Sign in to Spotify on the PC"));
	if (access_token_ && clock_() + refresh_margin_ < access_expires_at_)
		return Result<std::string>::success(*access_token_);
	return refresh_locked();
}

Result<std::string> SpotifySession::refresh_locked()
{
	using Output = Result<std::string>;
	const auto body = "grant_type=refresh_token&refresh_token=" + form_encode(*refresh_token_) +
			  "&client_id=" + form_encode(settings_.client_id);
	auto response = transport_(form_post(settings_, body));
	if (!response.has_value())
		return Output::failure(transport_error(response.error()));
	const auto json = json_body(response.value());
	const auto status = response.value().status;
	if (status != 200) {
		const auto code = json ? text(*json, "error") : std::string();
		if (code == "invalid_grant") {
			// Access was removed, or the token was rotated away: sign in again.
			sign_out_locked();
			(void)store_.clear();
			return Output::failure(error("MUSIC.SIGN_IN_REQUIRED", "The refresh token is no longer valid"));
		}
		if (code == "invalid_client")
			return Output::failure(error("MUSIC.CLIENT_INVALID", "Spotify does not know this app's client ID"));
		return Output::failure(error("MUSIC.REFRESH_FAILED", "Spotify refused the refresh", status >= 500 || status == 429));
	}
	auto tokens = json ? token_pair(*json, clock_()) : std::nullopt;
	if (!tokens)
		return Output::failure(error("MUSIC.TOKEN_RESPONSE_INVALID", "The refresh answer is incomplete", true));
	if (tokens->refresh_token && *tokens->refresh_token != *refresh_token_) {
		auto saved = store_.save(settings_.client_id, *tokens->refresh_token);
		if (!saved.has_value()) {
			// The token on disk may be rotated away; using the new access token would
			// leave no way to refresh again.
			sign_out_locked();
			return Output::failure(error("MUSIC.SIGN_IN_REQUIRED", "The rotated refresh token could not be saved"));
		}
		refresh_token_ = tokens->refresh_token;
	}
	++refreshes_;
	access_token_ = tokens->access_token;
	access_expires_at_ = tokens->access_expires_at;
	return Output::success(*access_token_);
}
} // namespace music_control
