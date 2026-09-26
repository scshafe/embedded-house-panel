#include "bhp/structured_logger.h"
#include "fixtures/loopback_http_server.h"
#include "vocabulary_panel/http_lexinet_client.h"
#include "vocabulary_panel/study_wire_codec.h"
#include "vocabulary_panel/study_worker.h"
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
using namespace vocabulary_panel;
namespace
{
int failures = 0, assertions = 0;
void expect(bool value, const char *message)
{
	++assertions;
	if (!value) {
		++failures;
		std::cerr << "FAILED: " << message << '\n';
	}
}
std::string fixture(const std::string &name)
{
	std::ifstream f(std::filesystem::path(__FILE__).parent_path() /
			"fixtures/study_v1/examples" / name);
	return {std::istreambuf_iterator<char>(f), {}};
}
HttpLexinetConfig config(const std::string &base)
{
	HttpLexinetConfig c;
	c.lexinet_base_url = base;
	c.transport_budget = {500, 2000, 1048576};
	c.maximum_snapshot_age_seconds = 3600;
	c.allow_insecure_http_for_tests = true;
	return c;
}
StudyCreateRequest create_request()
{
	StudyCreateRequest r;
	r.creation_request_id = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
	r.profile_ref = "fixture-default";
	r.topic_refs = {"fixture-systems", "fixture-words"};
	return r;
}
void test_tls(std::shared_ptr<bhp::http_runtime::HttpRuntime> runtime)
{
	using Server = test_fixture::LoopbackHttpServer;
	{
		Server server(Server::Transport::https, 200, fixture("session-created.json"));
		std::ostringstream logs;
		bhp::StructuredLogger logger("test.study", logs);
		auto c = config(server.base_url());
		c.allow_insecure_http_for_tests = false;
		c.trusted_ca_certificate_pem_for_tests = server.trusted_ca_certificate_pem();
		HttpLexinetClient client(runtime, c, logger);
		auto result = client.create_or_resume(create_request(), "trusted-tls");
		expect(result.has_value() && result.value().cards.size() == 1,
		       "full exported study.v1 projection crosses verified HTTPS");
		expect(server.tls_handshake_attempted() && server.server_error().empty(),
		       "TLS fixture records clean handshake");
	}
	{
		Server server(Server::Transport::https, 200, fixture("session-created.json"));
		std::ostringstream logs;
		bhp::StructuredLogger logger("test.study", logs);
		auto c = config(server.base_url());
		c.allow_insecure_http_for_tests = false;
		HttpLexinetClient client(runtime, c, logger);
		auto result = client.create_or_resume(create_request(), "untrusted-tls");
		expect(!result.has_value() && result.error().error_code ==
						  "LEXINET.TLS_CERTIFICATE_VERIFICATION_FAILED",
		       "untrusted TLS is rejected");
	}
	{
		Server server(Server::Transport::https, 200, fixture("session-created.json"), {},
			      Server::TlsCertificateIdentity::localhost_dns);
		std::ostringstream logs;
		bhp::StructuredLogger logger("test.study", logs);
		auto c = config(server.base_url());
		c.trusted_ca_certificate_pem_for_tests = server.trusted_ca_certificate_pem();
		HttpLexinetClient client(runtime, c, logger);
		auto result = client.create_or_resume(create_request(), "wrong-hostname");
		expect(!result.has_value() && result.error().error_code ==
						  "LEXINET.TLS_CERTIFICATE_VERIFICATION_FAILED",
		       "trusted wrong hostname rejected");
	}
}
void test_redirect_size_and_schema(std::shared_ptr<bhp::http_runtime::HttpRuntime> runtime)
{
	using Server = test_fixture::LoopbackHttpServer;
	{
		Server destination(Server::Transport::http, 200, fixture("session-created.json"));
		Server server(Server::Transport::http, 302, "{}",
			      destination.base_url() + "/redirect");
		std::ostringstream logs;
		bhp::StructuredLogger logger("test.study", logs);
		HttpLexinetClient client(runtime, config(server.base_url()), logger);
		auto result = client.create_or_resume(create_request(), "redirect");
		expect(!result.has_value() &&
			   result.error().error_code == "LEXINET.HTTP_STATUS_REJECTED" &&
			   destination.accepted_connection_count() == 0,
		       "redirect refused without credential forwarding");
	}
	{
		Server server(Server::Transport::http, 200, std::string(65, 'x'));
		std::ostringstream logs;
		bhp::StructuredLogger logger("test.study", logs);
		auto c = config(server.base_url());
		c.transport_budget.maximum_response_bytes = 64;
		HttpLexinetClient client(runtime, c, logger);
		auto result = client.create_or_resume(create_request(), "size-limit");
		expect(!result.has_value() &&
			   result.error().error_code == "LEXINET.RESPONSE_SIZE_LIMIT_EXCEEDED",
		       "streaming response byte bound enforced");
	}
	{
		Server server(Server::Transport::http, 200,
			      fixture("invalid/session-unknown-required-semantic.json"));
		std::ostringstream logs;
		bhp::StructuredLogger logger("test.study", logs);
		HttpLexinetClient client(runtime, config(server.base_url()), logger);
		auto result = client.create_or_resume(create_request(), "unknown-contract");
		expect(!result.has_value() &&
			   result.error().error_code == "LEXINET.RESPONSE_SCHEMA_INVALID",
		       "unknown required semantic refused through HTTPS adapter boundary");
	}
	{
		Server server(Server::Transport::http, 200, fixture("session-created.json"));
		std::ostringstream logs;
		bhp::StructuredLogger logger("test.study", logs);
		auto c = config(server.base_url());
		c.clock_trusted = true;
		c.current_utc_seconds = 1790294400;
		HttpLexinetClient client(runtime, c, logger);
		auto result = client.create_or_resume(create_request(), "stale-clock");
		expect(!result.has_value() && result.error().error_code == "LEXINET.SNAPSHOT_STALE",
		       "trusted clock rejects stale server projection");
	}
}
void test_worker_and_cancellation(std::shared_ptr<bhp::http_runtime::HttpRuntime> runtime)
{
	using Server = test_fixture::LoopbackHttpServer;
	Server server(Server::Transport::https, 200, fixture("session-created.json"));
	std::ostringstream logs;
	bhp::StructuredLogger logger("test.study", logs);
	auto c = config(server.base_url());
	c.trusted_ca_certificate_pem_for_tests = server.trusted_ca_certificate_pem();
	auto client = std::make_shared<HttpLexinetClient>(runtime, c, logger);
	{
		bhp::http_runtime::CancellationToken cancel;
		cancel.cancel();
		auto result =
		    client->create_or_resume(create_request(), "cancelled-before-request", cancel);
		expect(!result.has_value() && result.error().error_code == "LEXINET.CANCELLED" &&
			   server.accepted_connection_count() == 0,
		       "cancellation prevents network work");
	}
	{
		StudyWorker worker(runtime, client);
		expect(worker.create_or_resume(create_request(), "worker-request").has_value(),
		       "worker accepts bounded request");
		expect(!worker.create_or_resume(create_request(), "busy-request").has_value(),
		       "worker refuses queued future work");
		std::optional<StudyWorkerCompletion> completion;
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
		while (!completion && std::chrono::steady_clock::now() < deadline) {
			completion = worker.take_completion();
			std::this_thread::yield();
		}
		expect(completion && completion->session && !completion->error,
		       "worker completion returns parsed session to UI thread");
	}
}
void test_request_deadline_and_inflight_cancellation(
    std::shared_ptr<bhp::http_runtime::HttpRuntime> runtime)
{
	using Server = test_fixture::LoopbackHttpServer;
	{
		Server server(Server::Transport::http, 200, fixture("session-created.json"), {},
			      Server::TlsCertificateIdentity::loopback_ip, 250);
		std::ostringstream logs;
		bhp::StructuredLogger logger("test.study", logs);
		auto c = config(server.base_url());
		c.transport_budget = {20, 60, 1048576};
		HttpLexinetClient client(runtime, c, logger);
		const auto start = std::chrono::steady_clock::now();
		auto result = client.create_or_resume(create_request(), "request-deadline");
		expect(!result.has_value() && result.error().error_code == "LEXINET.TIMEOUT",
		       "whole request deadline returns typed timeout");
		expect(std::chrono::steady_clock::now() - start < std::chrono::seconds(1),
		       "retry does not reset whole request deadline");
	}
	{
		Server server(Server::Transport::http, 200, fixture("session-created.json"), {},
			      Server::TlsCertificateIdentity::loopback_ip, 500);
		std::ostringstream logs;
		bhp::StructuredLogger logger("test.study", logs);
		HttpLexinetClient client(runtime, config(server.base_url()), logger);
		bhp::http_runtime::CancellationToken cancel;
		std::optional<VocabularyPanelResult<StudySession>> result;
		std::thread request([&] {
			result =
			    client.create_or_resume(create_request(), "inflight-cancel", cancel);
		});
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
		while (server.accepted_connection_count() == 0 &&
		       std::chrono::steady_clock::now() < deadline)
			std::this_thread::yield();
		cancel.cancel();
		request.join();
		expect(result && !result->has_value() &&
			   result->error().error_code == "LEXINET.CANCELLED",
		       "inflight HTTP cancellation cannot publish parsed response");
	}
}

} // namespace
int main()
{
	std::signal(SIGPIPE, SIG_IGN);
	auto created = bhp::http_runtime::HttpRuntime::create();
	if (!created.has_value())
		return 1;
	auto runtime = created.value();
	test_tls(runtime);
	test_redirect_size_and_schema(runtime);
	test_worker_and_cancellation(runtime);
	test_request_deadline_and_inflight_cancellation(runtime);
	expect(runtime->shutdown().has_value(),
	       "all clients workers and handles destroyed before cleanup");
	std::cout << "http_lexinet_client: " << assertions << " assertions, " << failures
		  << " failures\n";
	return failures ? 1 : 0;
}
