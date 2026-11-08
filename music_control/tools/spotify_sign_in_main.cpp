// bhp-music-spotify-sign-in: the owner's one-time Spotify sign-in (ADR 0013).
// Run it on the owner's PC, in the owner's own account. It listens on
// 127.0.0.1 only, prints the address to open in a browser on the same PC,
// takes the one redirect, exchanges the code with the PKCE verifier it kept
// in memory, and writes the refresh token straight into
// <token-dir>/spotify-token.json (0600, in a 0700 directory). It prints where
// it saved the token, never the token, the code or the verifier.
#include "bhp/http_runtime/http_request.h"
#include "music_control/spotify_auth.h"
#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <nlohmann/json.hpp>
#include <poll.h>
#include <string>
#include <sys/random.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

namespace
{
using namespace music_control;
using Json = nlohmann::json;

const char *kUsage = "usage: bhp-music-spotify-sign-in --client-id ID --token-dir DIR [--port PORT]\n"
		     "  Register http://127.0.0.1:PORT/callback (default PORT 8888) as the app's redirect URI.\n"
		     "  DIR must not hold a spotify-token.json yet; keep it on a tmpfs such as /run/user/$UID.\n";
constexpr auto kSignInWindow = std::chrono::minutes(5);

int fail(const std::string &code, const std::string &message)
{
	std::cerr << Json({{"error_code", code}, {"message", message}}).dump() << "\n";
	return 1;
}

bool random_bytes(unsigned char *bytes, std::size_t size)
{
	std::size_t filled = 0;
	while (filled < size) {
		const auto got = getrandom(bytes + filled, size - filled, 0);
		if (got < 0 && errno == EINTR)
			continue;
		if (got <= 0)
			return false;
		filled += static_cast<std::size_t>(got);
	}
	return true;
}

int listen_loopback(unsigned port)
{
	const int listener = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (listener < 0)
		return -1;
	const int yes = 1;
	::setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
	sockaddr_in address{};
	address.sin_family = AF_INET;
	address.sin_port = htons(static_cast<std::uint16_t>(port));
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (::bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof address) != 0 || ::listen(listener, 4) != 0) {
		::close(listener);
		return -1;
	}
	return listener;
}

/** The request target of one HTTP/1.x GET, read within a few seconds; empty when it is anything else. */
std::string read_target(int connection)
{
	std::string request;
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (request.find("\r\n\r\n") == std::string::npos && request.size() < 8192) {
		const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
		pollfd readable{connection, POLLIN, 0};
		if (left.count() <= 0 || ::poll(&readable, 1, static_cast<int>(left.count())) <= 0)
			return {};
		char buffer[2048];
		const auto got = ::recv(connection, buffer, sizeof buffer, 0);
		if (got <= 0)
			return {};
		request.append(buffer, static_cast<std::size_t>(got));
	}
	const auto line_end = request.find("\r\n");
	if (line_end == std::string::npos || request.rfind("GET ", 0) != 0)
		return {};
	const auto line = request.substr(4, line_end - 4);
	const auto space = line.find(' ');
	if (space == std::string::npos || line.compare(space + 1, 7, "HTTP/1.") != 0)
		return {};
	return line.substr(0, space);
}

void respond(int connection, int status, const std::string &message)
{
	const std::string body = "<!doctype html><meta charset=utf-8><title>House panel</title><p>" + message + "</p>\n";
	const std::string head = "HTTP/1.1 " + std::to_string(status) + (status == 200 ? " OK" : status == 404 ? " Not Found" : " Bad Request") +
				 "\r\nContent-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\nConnection: close\r\n"
				 "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n";
	const auto text = head + body;
	(void)::send(connection, text.data(), text.size(), MSG_NOSIGNAL);
}

/** Spotify's token endpoint, or a loopback HTTP one for the tool's own test. */
bool allowed_token_url(const std::string &url)
{
	return url == "https://accounts.spotify.com/api/token" || url.rfind("http://127.0.0.1:", 0) == 0;
}
} // namespace

int main(int argc, char **argv)
{
	std::string client_id, token_dir, token_url = "https://accounts.spotify.com/api/token";
	unsigned port = 8888;
	for (int at = 1; at < argc; ++at) {
		const std::string option = argv[at];
		if (at + 1 >= argc) {
			std::cerr << kUsage;
			return 2;
		}
		const std::string value = argv[++at];
		if (option == "--client-id")
			client_id = value;
		else if (option == "--token-dir")
			token_dir = value;
		else if (option == "--port" && !value.empty() && value.size() <= 5 &&
			 value.find_first_not_of("0123456789") == std::string::npos)
			port = static_cast<unsigned>(std::stoul(value));
		else if (option == "--token-url")
			token_url = value;
		else {
			std::cerr << kUsage;
			return 2;
		}
	}
	const auto redirect_uri = "http://127.0.0.1:" + std::to_string(port) + "/callback";
	if (!valid_client_id(client_id) || token_dir.empty() || token_dir[0] != '/' || !valid_loopback_redirect(redirect_uri) ||
	    !allowed_token_url(token_url)) {
		std::cerr << kUsage;
		return 2;
	}
	std::signal(SIGPIPE, SIG_IGN);
	::umask(077);
	// The token directory's parents, owner-only; the store makes the directory itself.
	for (auto slash = token_dir.find('/', 1); slash != std::string::npos; slash = token_dir.find('/', slash + 1))
		if (::mkdir(token_dir.substr(0, slash).c_str(), 0700) != 0 && errno != EEXIST)
			return fail("MUSIC.TOKEN_DIRECTORY_UNAVAILABLE", "Cannot create the token directory's parent");
	struct stat existing {};
	if (::stat((token_dir + "/spotify-token.json").c_str(), &existing) == 0)
		return fail("MUSIC.TOKEN_EXISTS", "The directory already holds a token; use a new directory");

	SpotifyAuthSettings settings;
	settings.client_id = client_id;
	settings.token_url = token_url;
	const int listener = listen_loopback(port);
	if (listener < 0)
		return fail("MUSIC.SIGN_IN_PORT_UNAVAILABLE", "Cannot listen on 127.0.0.1:" + std::to_string(port));
	auto started = start_pkce(settings, redirect_uri, random_bytes);
	if (!started.has_value()) {
		::close(listener);
		return fail(started.error().code, started.error().message);
	}
	std::cout << "Open this address in a browser on this PC and approve the house panel:\n\n"
		  << started.value().authorize_url << "\n\nWaiting up to 5 minutes for Spotify to send you back here.\n"
		  << std::flush;

	auto created = bhp::http_runtime::HttpRuntime::create();
	if (!created.has_value()) {
		::close(listener);
		return fail(created.error().code, created.error().message);
	}
	auto runtime = created.value();
	const HttpTransport transport = [runtime](const bhp::http_runtime::HttpRequest &request) {
		return bhp::http_runtime::perform(*runtime, request, runtime->cancellation());
	};
	const auto deadline = std::chrono::steady_clock::now() + kSignInWindow;
	int status = 0;
	bool finished = false;
	while (!finished) {
		const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
		pollfd readable{listener, POLLIN, 0};
		if (left.count() <= 0 || ::poll(&readable, 1, static_cast<int>(left.count())) <= 0) {
			status = fail("MUSIC.SIGN_IN_TIMEOUT", "No redirect arrived within 5 minutes");
			break;
		}
		const int connection = ::accept4(listener, nullptr, nullptr, SOCK_CLOEXEC);
		if (connection < 0)
			continue;
		const auto target = read_target(connection);
		if (target.rfind("/callback?", 0) != 0) {
			// A browser also asks for /favicon.ico; anything but the callback waits on.
			respond(connection, 404, "Not here.");
			::close(connection);
			continue;
		}
		finished = true;
		auto code = authorization_code(target, started.value().state);
		if (!code.has_value()) {
			respond(connection, 400, "The sign-in did not complete (" + code.error().code + "). Close this tab.");
			::close(connection);
			status = fail(code.error().code, code.error().message);
			break;
		}
		const auto clock = [] { return std::chrono::steady_clock::now(); };
		auto tokens = exchange_code(settings, redirect_uri, code.value(), started.value().verifier, transport, clock);
		if (!tokens.has_value()) {
			respond(connection, 400, "Spotify refused the sign-in (" + tokens.error().code + "). Close this tab.");
			::close(connection);
			const auto oauth = tokens.error().context.find("oauth_error");
			status = fail(tokens.error().code,
				      tokens.error().message + (oauth == tokens.error().context.end() ? "" : ": " + oauth->second));
			break;
		}
		SpotifyTokenStore store(token_dir);
		auto saved = store.save(client_id, *tokens.value().refresh_token);
		if (!saved.has_value()) {
			respond(connection, 400, "The token could not be saved (" + saved.error().code + "). Close this tab.");
			::close(connection);
			status = fail(saved.error().code, saved.error().message);
			break;
		}
		respond(connection, 200, "Signed in. You can close this tab and return to the terminal.");
		::close(connection);
		std::cout << Json({{"result", "saved"}, {"path", token_dir + "/spotify-token.json"}}).dump() << "\n";
	}
	::close(listener);
	(void)runtime->shutdown();
	return status;
}
