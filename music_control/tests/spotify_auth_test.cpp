// PKCE sign-in, the token store and the session's refresh, against the scripted
// Spotify token endpoint and the fixtures in tests/fixtures/spotify.
#include "fake_spotify.hpp"
#include <atomic>
#include <filesystem>
#include <sys/stat.h>
#include <unistd.h>
using namespace music_control;
using namespace music_control_test;
namespace
{
std::string temporary_directory()
{
	char pattern[] = "/tmp/bhp-music-auth-XXXXXX";
	const char *made = ::mkdtemp(pattern);
	if (!made)
		std::exit(2);
	return made;
}

std::string read_file(const std::string &path)
{
	std::ifstream input(path, std::ios::binary);
	return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

bool no_secret(const Error &error)
{
	auto clean = [](const std::string &text) {
		return text.find("fixture-refresh") == std::string::npos && text.find("fixture-access") == std::string::npos &&
		       text.find("fixture-code") == std::string::npos;
	};
	if (!clean(error.message))
		return false;
	for (const auto &[key, value] : error.context)
		if (!clean(key) || !clean(value))
			return false;
	return true;
}

SpotifyAuthSettings settings()
{
	SpotifyAuthSettings value;
	value.client_id = kClientId;
	return value;
}

void pkce()
{
	// RFC 7636 appendix B.
	expect(code_challenge("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk") == "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM",
	       "S256 matches RFC 7636's example");
	unsigned char counter = 0;
	const RandomBytes random = [&counter](unsigned char *bytes, std::size_t size) {
		for (std::size_t i = 0; i < size; ++i)
			bytes[i] = counter++;
		return true;
	};
	auto started = start_pkce(settings(), "http://127.0.0.1:8888/callback", random);
	expect(started.has_value(), "PKCE starts");
	if (!started.has_value())
		return;
	const auto &start = started.value();
	expect(start.verifier.size() == 64 && start.state.size() == 32, "verifier is 64 characters, state 32");
	expect(start.verifier.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_") ==
		       std::string::npos,
	       "verifier uses only unreserved characters");
	const auto &url = start.authorize_url;
	expect(url.rfind("https://accounts.spotify.com/authorize?response_type=code&client_id=" + kClientId, 0) == 0,
	       "authorize URL names the client");
	expect(url.find("&scope=user-read-playback-state%20user-modify-playback-state&") != std::string::npos,
	       "authorize URL asks for the two playback scopes only");
	expect(url.find("&redirect_uri=http%3A%2F%2F127.0.0.1%3A8888%2Fcallback&") != std::string::npos,
	       "authorize URL carries the loopback redirect");
	expect(url.find("&code_challenge_method=S256&code_challenge=" + code_challenge(start.verifier) + "&") !=
		       std::string::npos,
	       "authorize URL carries the S256 challenge");
	expect(url.find(start.verifier) == std::string::npos, "authorize URL never carries the verifier");
	expect(url.find("&state=" + start.state) != std::string::npos, "authorize URL carries the state");

	for (const auto *bad : {"http://localhost:8888/callback", "https://127.0.0.1:8888/callback",
				"http://127.0.0.1:80/callback", "http://127.0.0.1:8888/other", "http://127.0.0.1:08888/callback"})
		expect(!start_pkce(settings(), bad, random).has_value(), std::string("redirect refused: ") + bad);
	auto bad_client = settings();
	bad_client.client_id = "short";
	expect(!start_pkce(bad_client, "http://127.0.0.1:8888/callback", random).has_value(), "bad client ID refused");
	auto no_random = start_pkce(settings(), "http://127.0.0.1:8888/callback",
				    [](unsigned char *, std::size_t) { return false; });
	expect(!no_random.has_value() && no_random.error().code == "MUSIC.RANDOM_UNAVAILABLE", "no randomness refused");
}

void redirect()
{
	auto code = authorization_code("/callback?code=AQB%2Dfixture-code_1&state=S1", "S1");
	expect(code.has_value() && code.value() == "AQB-fixture-code_1", "code read and percent-decoded");
	expect(authorization_code("/callback?code=x&state=S2", "S1").error().code == "MUSIC.SIGN_IN_STATE_MISMATCH",
	       "another state is refused");
	expect(authorization_code("/callback?code=x", "S1").error().code == "MUSIC.SIGN_IN_STATE_MISMATCH",
	       "a missing state is refused");
	expect(authorization_code("/callback?error=access_denied&state=S1", "S1").error().code == "MUSIC.SIGN_IN_DENIED",
	       "a denial is reported");
	expect(authorization_code("/callback?error=access_denied&state=S9", "S1").error().code ==
		       "MUSIC.SIGN_IN_STATE_MISMATCH",
	       "a denial with another state is a mismatch first");
	expect(authorization_code("/callback?state=S1", "S1").error().code == "MUSIC.SIGN_IN_INVALID", "no code refused");
	expect(authorization_code("/other?code=x&state=S1", "S1").error().code == "MUSIC.SIGN_IN_INVALID",
	       "another path refused");
	expect(authorization_code("/callback?code=a%2&state=S1", "S1").error().code == "MUSIC.SIGN_IN_INVALID",
	       "a broken escape refused");
}

void exchange()
{
	ManualClock clock;
	FakeSpotify spotify;
	HttpTransport transport = std::ref(spotify);
	const std::string redirect_uri = "http://127.0.0.1:8888/callback";
	spotify.token(200, fixture("token-authorization-code.json"));
	auto tokens = exchange_code(settings(), redirect_uri, "fixture-code", "verifier-1", transport, clock.function());
	expect(tokens.has_value() && tokens.value().refresh_token == std::string("fixture-refresh-1") &&
		       tokens.value().access_token == "fixture-access-1",
	       "the code exchange returns both tokens");
	if (tokens.has_value())
		expect(tokens.value().access_expires_at == clock.now + std::chrono::seconds(3600), "expiry from expires_in");
	const auto &request = spotify.requests.back();
	const auto fields = form(request.body);
	expect(request.method == HttpMethod::Post && request.url == kTokenUrl, "POST to the token endpoint");
	expect(fields.at("grant_type") == "authorization_code" && fields.at("code") == "fixture-code" &&
		       fields.at("client_id") == kClientId && fields.at("code_verifier") == "verifier-1" &&
		       fields.at("redirect_uri") == "http%3A%2F%2F127.0.0.1%3A8888%2Fcallback" && fields.size() == 5,
	       "the exchange sends exactly the PKCE fields");
	bool authorization = false;
	for (const auto &header : request.headers)
		authorization = authorization || header.first == "Authorization";
	expect(!authorization, "no client secret or Authorization header is ever sent");

	spotify.token(200, R"({"access_token":"a","token_type":"Bearer","scope":"user-read-playback-state","expires_in":3600,"refresh_token":"r"})");
	auto narrow = exchange_code(settings(), redirect_uri, "c", "v", transport, clock.function());
	expect(!narrow.has_value() && narrow.error().code == "MUSIC.SIGN_IN_SCOPE_MISSING", "a missing scope is refused");
	spotify.token(200, fixture("token-refresh-kept.json"));
	auto no_refresh = exchange_code(settings(), redirect_uri, "c", "v", transport, clock.function());
	expect(!no_refresh.has_value() && no_refresh.error().code == "MUSIC.TOKEN_RESPONSE_INVALID",
	       "a sign-in without a refresh token is refused");
	spotify.token(400, fixture("token-invalid-grant.json"));
	auto refused = exchange_code(settings(), redirect_uri, "fixture-code", "v", transport, clock.function());
	expect(!refused.has_value() && refused.error().code == "MUSIC.SIGN_IN_REFUSED" &&
		       refused.error().context.at("oauth_error") == "invalid_grant" && no_secret(refused.error()),
	       "a refused code reports the OAuth error only");
	spotify.unreachable = true;
	auto down = exchange_code(settings(), redirect_uri, "c", "v", transport, clock.function());
	expect(!down.has_value() && down.error().code == "MUSIC.SPOTIFY_UNREACHABLE" && down.error().retryable,
	       "no answer is retryable");
}

void store()
{
	const auto directory = temporary_directory() + "/token";
	SpotifyTokenStore store(directory);
	auto empty = store.load(kClientId);
	expect(empty.has_value() && !empty.value(), "a new store holds no token");
	expect(store.save(kClientId, "fixture-refresh-1").has_value(), "save");
	struct stat file {}, folder {};
	::stat((directory + "/spotify-token.json").c_str(), &file);
	::stat(directory.c_str(), &folder);
	expect((file.st_mode & 0777) == 0600, "the token file is 0600");
	expect((folder.st_mode & 0777) == 0700, "the token directory is 0700");
	auto loaded = store.load(kClientId);
	expect(loaded.has_value() && loaded.value() == std::string("fixture-refresh-1"), "load returns the token");
	auto other = store.load("fedcba9876543210fedcba9876543210");
	expect(!other.has_value() && other.error().code == "MUSIC.STORAGE_MISMATCH" && no_secret(other.error()),
	       "a token saved for another client is never offered");
	expect(store.clear().has_value(), "clear");
	auto cleared = store.load(kClientId);
	expect(cleared.has_value() && !cleared.value(), "a cleared store holds no token");
	expect(read_file(directory + "/spotify-token.json").find("fixture-refresh") == std::string::npos,
	       "clearing removes the token from disk");
}

void session()
{
	const auto directory = temporary_directory() + "/token";
	ManualClock clock;
	FakeSpotify spotify;
	SpotifyTokenStore store(directory);
	(void)store.save(kClientId, "fixture-refresh-1");
	SpotifySession session(settings(), std::ref(spotify), store, clock.function());
	expect(session.restore().has_value() && session.signed_in(), "restore signs in from the store");

	// A refresh that rotates: the new token is on disk before the access token is used.
	spotify.token(200, fixture("token-refresh-rotated.json"));
	auto first = session.access_token();
	expect(first.has_value() && first.value() == "fixture-access-2", "refresh returns the access token");
	const auto fields = form(spotify.requests.back().body);
	expect(fields.at("grant_type") == "refresh_token" && fields.at("refresh_token") == "fixture-refresh-1" &&
		       fields.at("client_id") == kClientId && fields.size() == 3,
	       "the refresh sends the PKCE refresh fields only");
	expect(read_file(directory + "/spotify-token.json").find("fixture-refresh-2") != std::string::npos,
	       "the rotated token is saved");
	auto cached = session.access_token();
	expect(cached.has_value() && spotify.token_requests() == 1, "a fresh access token is reused");

	// Near expiry it refreshes; an answer without a refresh token keeps the saved one.
	clock.now += std::chrono::seconds(3550);
	spotify.token(200, fixture("token-refresh-kept.json"));
	auto kept = session.access_token();
	expect(kept.has_value() && kept.value() == "fixture-access-3", "refresh within the margin");
	expect(form(spotify.requests.back().body).at("refresh_token") == "fixture-refresh-2", "the rotated token is used");
	expect(read_file(directory + "/spotify-token.json").find("fixture-refresh-2") != std::string::npos,
	       "a refresh without a new token keeps the saved one");

	// After a 401 the access token is forgotten.
	session.invalidate_access_token();
	spotify.token(503, "");
	auto unavailable = session.access_token();
	expect(!unavailable.has_value() && unavailable.error().code == "MUSIC.REFRESH_FAILED" &&
		       unavailable.error().retryable && session.signed_in(),
	       "a 503 refresh is retryable and keeps the sign-in");
	spotify.token(401, R"({"error":"invalid_client","error_description":"Invalid client"})");
	auto client = session.access_token();
	expect(!client.has_value() && client.error().code == "MUSIC.CLIENT_INVALID" && session.signed_in(),
	       "invalid_client is reported without discarding the token");
	spotify.token(400, fixture("token-invalid-grant.json"));
	auto revoked = session.access_token();
	expect(!revoked.has_value() && revoked.error().code == "MUSIC.SIGN_IN_REQUIRED" && !session.signed_in() &&
		       no_secret(revoked.error()),
	       "invalid_grant signs out");
	auto after = store.load(kClientId);
	expect(after.has_value() && !after.value(), "invalid_grant clears the store");
	auto signed_out = session.access_token();
	expect(!signed_out.has_value() && signed_out.error().code == "MUSIC.SIGN_IN_REQUIRED",
	       "signed out, nothing is sent");
}

void single_flight()
{
	const auto directory = temporary_directory() + "/token";
	FakeSpotify spotify;
	spotify.token_delay = std::chrono::milliseconds(50);
	SpotifyTokenStore store(directory);
	(void)store.save(kClientId, "fixture-refresh-1");
	SpotifySession session(settings(), std::ref(spotify), store, [] { return std::chrono::steady_clock::now(); });
	(void)session.restore();
	spotify.token(200, fixture("token-refresh-rotated.json"));
	std::atomic<int> ok{0};
	std::vector<std::thread> threads;
	for (int i = 0; i < 4; ++i)
		threads.emplace_back([&] { ok += session.access_token().has_value(); });
	for (auto &thread : threads)
		thread.join();
	expect(ok == 4 && spotify.token_requests() == 1 && session.refreshes() == 1, "concurrent callers share one refresh");
}

void failed_save()
{
	if (::geteuid() == 0)
		return; // root writes through the read-only directory; the check needs an ordinary user.
	const auto directory = temporary_directory() + "/token";
	FakeSpotify spotify;
	SpotifyTokenStore store(directory);
	(void)store.save(kClientId, "fixture-refresh-1");
	SpotifySession session(settings(), std::ref(spotify), store, [] { return std::chrono::steady_clock::now(); });
	(void)session.restore();
	::chmod(directory.c_str(), 0500);
	spotify.token(200, fixture("token-refresh-rotated.json"));
	auto refreshed = session.access_token();
	::chmod(directory.c_str(), 0700);
	expect(!refreshed.has_value() && refreshed.error().code == "MUSIC.SIGN_IN_REQUIRED" && !session.signed_in(),
	       "a rotated token that cannot be saved signs out instead of being used");
}
} // namespace

int main()
{
	pkce();
	redirect();
	exchange();
	store();
	session();
	single_flight();
	failed_save();
	if (failures == 0)
		std::cout << "music_control_spotify_auth_test passed\n";
	return failures == 0 ? 0 : 1;
}
