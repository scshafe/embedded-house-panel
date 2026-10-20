#pragma once
#include "bhp/durable_store/durable_store.h"
#include "bhp/http_runtime/http_request.h"
#include "voice_lab_recorder/result.h"
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <variant>

/**
 * The voice-lab uploader's Pocket ID sign-in (ADR 0012, recorder groundwork
 * plan Step 7). Device authorization grant (RFC 8628) for the public client
 * `voice-lab-panel`, with the Pocket ID v2.13 rules voice-lab recorded in its
 * D-056 design: endpoints from the issuer's discovery document, `client_id`
 * and no `resource`, `verification_uri` and the code only, `expires_in`
 * enforced here, refresh single-flight with the rotated token saved before
 * use. Tokens are opaque; nothing here logs, prints or returns a refresh token.
 */
namespace voice_lab_recorder
{
using Clock = std::function<std::chrono::steady_clock::time_point()>;
/** One HTTP exchange; production binds bhp::http_runtime::perform to the process runtime. */
using HttpTransport = std::function<bhp::http_runtime::Result<bhp::http_runtime::HttpResponse>(
	const bhp::http_runtime::HttpRequest &)>;

struct SignInSettings {
	/** The Pocket ID issuer, `https://HOST`, exactly as its discovery document names it. */
	std::string issuer;
	std::string client_id{"voice-lab-panel"};
	/** Optional; empty sends no scope. Never a `resource`. */
	std::string scope;
	bhp::http_runtime::TransportBudget budget{2000, 10000, 65536};
	std::string trusted_ca_pem_for_tests;
};

struct Endpoints {
	std::string device_authorization;
	std::string token;
};

/**
 * Reads `<issuer>/.well-known/openid-configuration`. The document must name
 * the same issuer, offer the device_code grant, and put both endpoints on the
 * issuer's origin over HTTPS (VOICE_LAB.DISCOVERY_INVALID otherwise).
 */
Result<Endpoints> discover(const SignInSettings &settings, const HttpTransport &transport);

/** What the panel shows: never the complete link (ADR 0012). */
struct DevicePrompt {
	std::string verification_uri;
	std::string user_code;
	std::chrono::steady_clock::time_point expires_at;
	std::chrono::seconds interval{5};
};

struct TokenPair {
	std::string access_token;
	std::string refresh_token;
	std::chrono::steady_clock::time_point access_expires_at;
};

/** One device sign-in, from the code request to approval or expiry. */
class DeviceCodeSignIn final
{
public:
	static Result<DeviceCodeSignIn> start(const SignInSettings &settings, const Endpoints &endpoints,
					      HttpTransport transport, Clock clock);
	const DevicePrompt &prompt() const { return prompt_; }

	enum class State { Pending, Approved, Expired };
	struct Poll {
		State state{State::Pending};
		std::optional<TokenPair> tokens;
	};
	/**
	 * Polls the token endpoint once if the interval has passed; before that it
	 * answers Pending without a request. After `expires_at` it answers Expired
	 * without a request, whatever the server would say.
	 */
	Result<Poll> poll();

private:
	DeviceCodeSignIn(SignInSettings settings, Endpoints endpoints, HttpTransport transport, Clock clock,
			 DevicePrompt prompt, std::string device_code)
		: settings_(std::move(settings)), endpoints_(std::move(endpoints)), transport_(std::move(transport)),
		  clock_(std::move(clock)), prompt_(std::move(prompt)), device_code_(std::move(device_code))
	{
	}
	SignInSettings settings_;
	Endpoints endpoints_;
	HttpTransport transport_;
	Clock clock_;
	DevicePrompt prompt_;
	std::string device_code_;
	std::chrono::steady_clock::time_point next_poll_{};
	bool finished_{false};
};

/**
 * @canonical voice_lab_refresh_token_store -- the one stored secret (ADR 0012).
 *
 * A DurableFile (`refresh-token.json`, owner-only directory, VOICE_LAB.STORAGE_*)
 * holding the refresh token with the issuer and client it belongs to. A token
 * saved for another issuer or client is never offered.
 */
class RefreshTokenStore final
{
public:
	explicit RefreshTokenStore(std::string directory);
	Result<std::optional<std::string>> load(const std::string &issuer, const std::string &client_id);
	Result<bool> save(const std::string &issuer, const std::string &client_id, const std::string &refresh_token);
	/** Replaces the token with a signed-out record. */
	Result<bool> clear();

private:
	bhp::durable_store::DurableFile file_;
};

/**
 * @canonical voice_lab_session -- the uploader's access tokens, refreshed single-flight.
 *
 * `access_token()` returns a token valid for at least `refresh_margin`; when
 * it must refresh, one caller refreshes while the others wait. The rotated
 * refresh token is saved durably before the new access token is returned; if
 * that save fails, the new access token is discarded and the session signs
 * out (the token on disk was already rotated away). `invalid_grant` signs out
 * and clears the store. Signed out, every call is VOICE_LAB.SIGN_IN_REQUIRED.
 */
class VoiceLabSession final
{
public:
	VoiceLabSession(SignInSettings settings, Endpoints endpoints, HttpTransport transport, RefreshTokenStore &store,
			Clock clock, std::chrono::seconds refresh_margin = std::chrono::seconds(60));
	/** Loads a saved refresh token; the session is signed in if there is one. */
	Result<bool> restore();
	/** Keeps a fresh sign-in: saves its refresh token durably first. */
	Result<bool> adopt(const TokenPair &tokens);
	Result<std::string> access_token();
	bool signed_in() const;
	/** Successful refresh exchanges so far, for tests and diagnostics. */
	unsigned refreshes() const;

private:
	Result<std::string> refresh_locked();
	void sign_out_locked();
	SignInSettings settings_;
	Endpoints endpoints_;
	HttpTransport transport_;
	RefreshTokenStore &store_;
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
} // namespace voice_lab_recorder
