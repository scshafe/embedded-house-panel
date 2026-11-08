#pragma once
#include "bhp/durable_store/durable_store.h"
#include "bhp/http_runtime/http_request.h"
#include "music_control/result.h"
#include <chrono>
#include <cstddef>
#include <functional>
#include <mutex>
#include <optional>
#include <string>

/**
 * The Spotify sign-in and refresh token (ADR 0013). Authorization Code with
 * PKCE for the owner's development-mode app: a public client, no client
 * secret, a loopback redirect URI. The refresh token is opaque; nothing here
 * logs, prints or returns it outside the store and the session.
 */
namespace music_control
{
using Clock = std::function<std::chrono::steady_clock::time_point()>;
/** One HTTP exchange; production binds bhp::http_runtime::perform to the process runtime. */
using HttpTransport = std::function<bhp::http_runtime::Result<bhp::http_runtime::HttpResponse>(
	const bhp::http_runtime::HttpRequest &)>;
/** Fills `size` bytes with cryptographic randomness; false when none is available. */
using RandomBytes = std::function<bool(unsigned char *bytes, std::size_t size)>;

/** The two playback scopes and nothing else (ADR 0013). */
inline constexpr const char *kSpotifyScopes = "user-read-playback-state user-modify-playback-state";

struct SpotifyAuthSettings {
	/** The owner's app's client ID: not a secret, but kept out of Git with the rest of the config. */
	std::string client_id;
	std::string authorize_url{"https://accounts.spotify.com/authorize"};
	std::string token_url{"https://accounts.spotify.com/api/token"};
	bhp::http_runtime::TransportBudget budget{2000, 10000, 65536};
};

/** 16 to 64 letters and digits, as Spotify client IDs are. */
bool valid_client_id(const std::string &client_id);
/** `http://127.0.0.1:PORT/callback` with a port from 1024 to 65535: Spotify's loopback rule. */
bool valid_loopback_redirect(const std::string &redirect_uri);

/** RFC 7636 S256: base64url without padding of SHA-256 of the verifier. */
std::string code_challenge(const std::string &verifier);

/** One sign-in's secrets, kept in the sign-in tool's memory only. */
struct PkceStart {
	std::string verifier;
	std::string state;
	/** What the owner opens: client ID, scopes, redirect, challenge and state; no secret. */
	std::string authorize_url;
};

/**
 * A 64-character verifier and a 32-character state from `random`, and the
 * authorize URL (MUSIC.SIGN_IN_INVALID for a bad client ID or redirect,
 * MUSIC.RANDOM_UNAVAILABLE without randomness).
 */
Result<PkceStart> start_pkce(const SpotifyAuthSettings &settings, const std::string &redirect_uri,
			     const RandomBytes &random);

/**
 * The code from the redirect's request target (`/callback?code=...&state=...`).
 * MUSIC.SIGN_IN_STATE_MISMATCH unless `state` matches exactly;
 * MUSIC.SIGN_IN_DENIED when Spotify sent `error` instead;
 * MUSIC.SIGN_IN_INVALID for anything else.
 */
Result<std::string> authorization_code(const std::string &request_target, const std::string &expected_state);

struct TokenPair {
	std::string access_token;
	/** Absent when a refresh kept the old refresh token. */
	std::optional<std::string> refresh_token;
	std::chrono::steady_clock::time_point access_expires_at;
	std::string scope;
};

/**
 * Exchanges the code with the verifier. The answer must carry a refresh token
 * and grant both playback scopes (MUSIC.SIGN_IN_SCOPE_MISSING otherwise).
 */
Result<TokenPair> exchange_code(const SpotifyAuthSettings &settings, const std::string &redirect_uri,
				const std::string &code, const std::string &verifier, const HttpTransport &transport,
				const Clock &clock);

/**
 * @canonical spotify_refresh_token_store -- the one stored Spotify secret (ADR 0013).
 *
 * A DurableFile (`spotify-token.json`, owner-only directory, MUSIC.STORAGE_*)
 * holding the refresh token with the client ID it belongs to. A token saved
 * for another client is never offered.
 */
class SpotifyTokenStore final
{
public:
	explicit SpotifyTokenStore(std::string directory);
	Result<std::optional<std::string>> load(const std::string &client_id);
	Result<bool> save(const std::string &client_id, const std::string &refresh_token);
	/** Replaces the token with a signed-out record. */
	Result<bool> clear();

private:
	bhp::durable_store::DurableFile file_;
};

/**
 * @canonical spotify_session -- the service's access tokens, refreshed single-flight.
 *
 * `access_token()` returns a token valid for at least `refresh_margin`; one
 * caller refreshes while the others wait. When a refresh returns a new
 * refresh token, it is saved durably before the new access token is
 * returned; if that save fails, the session signs out. `invalid_grant` signs
 * out and clears the store. Signed out, every call is MUSIC.SIGN_IN_REQUIRED.
 */
class SpotifySession final
{
public:
	SpotifySession(SpotifyAuthSettings settings, HttpTransport transport, SpotifyTokenStore &store, Clock clock,
		       std::chrono::seconds refresh_margin = std::chrono::seconds(60));
	/** Loads a saved refresh token; the session is signed in if there is one. */
	Result<bool> restore();
	Result<std::string> access_token();
	/** Forgets the access token, so the next `access_token()` refreshes: after a 401. */
	void invalidate_access_token();
	bool signed_in() const;
	/** Successful refresh exchanges so far, for tests. */
	unsigned refreshes() const;

private:
	Result<std::string> refresh_locked();
	void sign_out_locked();
	SpotifyAuthSettings settings_;
	HttpTransport transport_;
	SpotifyTokenStore &store_;
	Clock clock_;
	std::chrono::seconds refresh_margin_;
	mutable std::mutex mutex_;
	std::optional<std::string> refresh_token_;
	std::optional<std::string> access_token_;
	std::chrono::steady_clock::time_point access_expires_at_{};
	unsigned refreshes_{0};
};

/** application/x-www-form-urlencoded encoding of one value. */
std::string form_encode(const std::string &value);
} // namespace music_control
