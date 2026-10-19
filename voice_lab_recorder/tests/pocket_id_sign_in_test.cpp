// Device sign-in and the refresh token store against a fake Pocket ID that behaves as
// voice-lab's D-056 records v2.13.0: pending forever after expiry, never slow_down,
// rotation on every refresh with no grace, and family revocation on reuse.
#include "voice_lab_recorder/pocket_id_sign_in.h"
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <mutex>
#include <set>
#include <sys/stat.h>
#include <thread>
#include <vector>
using namespace voice_lab_recorder;
using bhp::http_runtime::HttpMethod;
using bhp::http_runtime::HttpRequest;
using bhp::http_runtime::HttpResponse;
namespace
{
int failures = 0;
void expect(bool value, const std::string &label)
{
	if (!value) {
		++failures;
		std::cerr << "FAIL: " << label << '\n';
	}
}

const std::string kIssuer = "https://id.example.ts.net";

struct FakePocketId {
	std::mutex mutex;
	std::vector<HttpRequest> requests;
	bool approved{false};
	std::string current_refresh{"refresh-0"};
	std::set<std::string> rotated;
	bool family_revoked{false};
	int generation{0};
	std::chrono::milliseconds token_delay{0};
	std::string discovery_issuer{kIssuer};
	std::string token_endpoint{kIssuer + "/api/oidc/token"};

	static std::map<std::string, std::string> form(const std::string &body)
	{
		std::map<std::string, std::string> fields;
		std::size_t at = 0;
		while (at <= body.size()) {
			const auto end = std::min(body.find('&', at), body.size());
			const auto pair = body.substr(at, end - at);
			fields[pair.substr(0, pair.find('='))] = pair.substr(pair.find('=') + 1);
			at = end + 1;
		}
		return fields;
	}
	static bhp::http_runtime::Result<HttpResponse> json(long status, const std::string &body)
	{
		return bhp::http_runtime::Result<HttpResponse>::success({status, "application/json", {}, body});
	}
	bhp::http_runtime::Result<HttpResponse> operator()(const HttpRequest &request)
	{
		std::unique_lock<std::mutex> lock(mutex);
		requests.push_back(request);
		if (request.method == HttpMethod::Get)
			return json(200, "{\"issuer\":\"" + discovery_issuer +
						 "\",\"device_authorization_endpoint\":\"" + kIssuer +
						 "/api/oidc/device/authorize\",\"token_endpoint\":\"" + token_endpoint +
						 "\",\"grant_types_supported\":[\"authorization_code\",\"refresh_token\","
						 "\"urn:ietf:params:oauth:grant-type:device_code\"]}");
		const auto fields = form(request.body);
		if (request.url == kIssuer + "/api/oidc/device/authorize")
			return json(200, "{\"device_code\":\"device-1\",\"user_code\":\"EABCDEFG\",\"verification_uri\":\"" + kIssuer +
						 "/device\",\"verification_uri_complete\":\"" + kIssuer +
						 "/device?user_code=EABCDEFG\",\"expires_in\":900,\"interval\":5}");
		if (fields.at("grant_type") == "urn%3Aietf%3Aparams%3Aoauth%3Agrant-type%3Adevice_code") {
			if (!approved)
				return json(400, "{\"error\":\"authorization_pending\"}");
			return json(200, "{\"access_token\":\"access-0\",\"refresh_token\":\"refresh-0\",\"expires_in\":900,"
					 "\"token_type\":\"Bearer\"}");
		}
		// refresh_token grant
		lock.unlock();
		std::this_thread::sleep_for(token_delay);
		lock.lock();
		const auto presented = fields.at("refresh_token");
		if (family_revoked || rotated.count(presented)) {
			family_revoked = true;
			return json(400, "{\"error\":\"invalid_grant\"}");
		}
		if (presented != current_refresh)
			return json(400, "{\"error\":\"invalid_grant\"}");
		rotated.insert(current_refresh);
		current_refresh = "refresh-" + std::to_string(++generation);
		return json(200, "{\"access_token\":\"access-" + std::to_string(generation) + "\",\"refresh_token\":\"" +
					 current_refresh + "\",\"expires_in\":900,\"token_type\":\"Bearer\"}");
	}
	std::size_t count()
	{
		std::lock_guard<std::mutex> lock(mutex);
		return requests.size();
	}
};

struct FakeClock {
	std::chrono::steady_clock::time_point now{std::chrono::steady_clock::time_point{} + std::chrono::hours(1)};
	Clock function()
	{
		return [this] { return now; };
	}
};

std::string temporary_directory()
{
	const char *base = std::getenv("TMPDIR");
	std::string pattern = std::string(base ? base : "/tmp") + "/voice-lab-sign-in-test-XXXXXX";
	if (mkdtemp(pattern.data()) == nullptr)
		std::abort();
	return pattern;
}

std::string read_all(const std::string &path)
{
	std::ifstream file(path);
	return std::string((std::istreambuf_iterator<char>(file)), {});
}

SignInSettings settings()
{
	SignInSettings value;
	value.issuer = kIssuer;
	return value;
}

void test_discovery()
{
	FakePocketId server;
	HttpTransport transport = [&server](const HttpRequest &r) { return server(r); };
	auto endpoints = discover(settings(), transport);
	expect(endpoints && endpoints.value().token == kIssuer + "/api/oidc/token", "discovery finds the endpoints");
	server.discovery_issuer = "https://other.example";
	expect(!discover(settings(), transport), "another issuer is refused");
	server.discovery_issuer = kIssuer;
	server.token_endpoint = "https://evil.example/token";
	auto cross = discover(settings(), transport);
	expect(!cross && cross.error().code == "VOICE_LAB.DISCOVERY_INVALID", "a cross-origin endpoint is refused");
	auto bad = settings();
	bad.issuer = "http://id.example";
	expect(!discover(bad, transport), "the issuer must be HTTPS");
}

void test_device_sign_in()
{
	FakePocketId server;
	FakeClock clock;
	HttpTransport transport = [&server](const HttpRequest &r) { return server(r); };
	const auto endpoints = discover(settings(), transport).value();
	auto started = DeviceCodeSignIn::start(settings(), endpoints, transport, clock.function());
	expect(started.has_value(), "device code issued");
	auto &sign_in = started.value();
	expect(sign_in.prompt().verification_uri == kIssuer + "/device" && sign_in.prompt().user_code == "EABCDEFG",
	       "the prompt is the plain URI and the code, never the complete link");
	const auto before = server.count();
	auto early = sign_in.poll();
	expect(early && early.value().state == DeviceCodeSignIn::State::Pending && server.count() == before,
	       "no poll before the interval");
	clock.now += std::chrono::seconds(5);
	auto pending = sign_in.poll();
	expect(pending && pending.value().state == DeviceCodeSignIn::State::Pending && server.count() == before + 1,
	       "pending after one poll");
	server.approved = true;
	clock.now += std::chrono::seconds(5);
	auto approved = sign_in.poll();
	expect(approved && approved.value().state == DeviceCodeSignIn::State::Approved &&
		       approved.value().tokens->refresh_token == "refresh-0",
	       "approval yields the token pair");
	for (const auto &request : server.requests) {
		expect(request.body.find("resource") == std::string::npos, "no resource parameter is ever sent");
		if (request.method == HttpMethod::Post)
			expect(request.body.find("client_id=voice-lab-panel") != std::string::npos, "client_id on every POST");
	}
}

void test_expiry_is_enforced_locally()
{
	FakePocketId server; // never approves: v2.13 keeps answering pending after expiry
	FakeClock clock;
	HttpTransport transport = [&server](const HttpRequest &r) { return server(r); };
	auto sign_in = DeviceCodeSignIn::start(settings(), discover(settings(), transport).value(), transport,
					       clock.function())
			       .value();
	for (int i = 0; i < 179; ++i) {
		clock.now += std::chrono::seconds(5);
		(void)sign_in.poll();
	}
	const auto polled = server.count();
	clock.now += std::chrono::seconds(5);
	auto expired = sign_in.poll();
	expect(expired && expired.value().state == DeviceCodeSignIn::State::Expired && server.count() == polled,
	       "after 15 minutes the client stops by itself, without a request");
}

void test_rotation_single_flight_and_reuse()
{
	const auto root = temporary_directory();
	FakePocketId server;
	FakeClock clock;
	HttpTransport transport = [&server](const HttpRequest &r) { return server(r); };
	const auto endpoints = discover(settings(), transport).value();
	RefreshTokenStore store(root + "/token");
	VoiceLabSession session(settings(), endpoints, transport, store, clock.function());
	expect(!session.access_token() && !session.signed_in(), "signed out until a sign-in is adopted");
	server.approved = true;
	expect(session.adopt({"access-0", "refresh-0", clock.now + std::chrono::seconds(900)}).has_value(), "adopt");
	struct stat status {};
	expect(stat((root + "/token/refresh-token.json").c_str(), &status) == 0 && (status.st_mode & 0777) == 0600,
	       "the token file is 0600");
	auto access = session.access_token();
	expect(access && access.value() == "access-0" && session.refreshes() == 0, "a fresh access token is reused");

	clock.now += std::chrono::seconds(900);
	server.token_delay = std::chrono::milliseconds(50);
	std::vector<std::string> results(4);
	std::vector<std::thread> callers;
	for (int i = 0; i < 4; ++i)
		callers.emplace_back([&, i] {
			auto token = session.access_token();
			results[i] = token ? token.value() : token.error().code;
		});
	for (auto &caller : callers)
		caller.join();
	expect(session.refreshes() == 1 && results == std::vector<std::string>(4, "access-1"),
	       "four concurrent callers cause exactly one refresh");
	const auto saved = read_all(root + "/token/refresh-token.json");
	expect(saved.find("refresh-1") != std::string::npos && saved.find("refresh-0") == std::string::npos,
	       "the rotated token replaced the old one on disk");

	// A copy of the saved token is used elsewhere first; the panel's next refresh is a reuse.
	(void)server(HttpRequest{HttpMethod::Post, kIssuer + "/api/oidc/token", {},
				 "grant_type=refresh_token&refresh_token=refresh-1&client_id=voice-lab-panel", {}, {}, {}});
	clock.now += std::chrono::seconds(900);
	auto reused = session.access_token();
	expect(!reused && reused.error().code == "VOICE_LAB.SIGN_IN_REQUIRED" && !session.signed_in(),
	       "invalid_grant signs the panel out");
	auto after = store.load(kIssuer, "voice-lab-panel");
	expect(after && !after.value().has_value(), "and clears the stored token");
	for (const auto &value : results)
		expect(value.find("refresh") == std::string::npos, "no refresh token reaches a caller");
	std::filesystem::remove_all(root);
}

void test_save_failure_signs_out_before_use()
{
	const auto root = temporary_directory();
	FakePocketId server;
	FakeClock clock;
	HttpTransport transport = [&server](const HttpRequest &r) { return server(r); };
	RefreshTokenStore store(root + "/token");
	VoiceLabSession session(settings(), discover(settings(), transport).value(), transport, store, clock.function());
	(void)session.adopt({"access-0", "refresh-0", clock.now + std::chrono::seconds(900)});
	std::filesystem::permissions(root + "/token", std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec);
	clock.now += std::chrono::seconds(900);
	auto refreshed = session.access_token();
	std::filesystem::permissions(root + "/token", std::filesystem::perms::owner_all);
	expect(!refreshed && refreshed.error().code == "VOICE_LAB.SIGN_IN_REQUIRED" && !session.signed_in(),
	       "a rotated token that cannot be saved is never used");
	RefreshTokenStore other(root + "/token-2");
	(void)other.save("https://elsewhere.example", "voice-lab-panel", "refresh-x");
	VoiceLabSession mismatched(settings(), discover(settings(), transport).value(), transport, other, clock.function());
	auto restored = mismatched.restore();
	expect(!restored && restored.error().code == "VOICE_LAB.STORAGE_MISMATCH",
	       "a token saved for another issuer is never offered");
	std::filesystem::remove_all(root);
}
} // namespace

int main()
{
	test_discovery();
	test_device_sign_in();
	test_expiry_is_enforced_locally();
	test_rotation_single_flight_and_reuse();
	test_save_failure_signs_out_before_use();
	std::cout << "pocket_id_sign_in: " << failures << " failures\n";
	return failures ? 1 : 0;
}
