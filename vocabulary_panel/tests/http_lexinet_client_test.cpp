#include "fixtures/loopback_http_server.h"
#include "vocabulary_panel/http_lexinet_client.h"

#include "bhp/structured_logger.h"

#include <csignal>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>

namespace {

int failures = 0;
int assertions = 0;

void expect(const bool condition, const std::string &scenario)
{
	++assertions;
	if (condition)
		return;
	std::cerr << "FAILED: " << scenario << '\n';
	++failures;
}

vocabulary_panel::HttpLexinetConfig test_config(const std::string &base_url)
{
	return {
		base_url,
		"/next",
		"/review",
		"",
		2000,
		262144,
		true,
		"",
	};
}

void test_trusted_matching_tls_certificate_succeeds()
{
	using vocabulary_panel::test_fixture::LoopbackHttpServer;
	LoopbackHttpServer server(
		LoopbackHttpServer::Transport::https, 200,
		R"({"data":null,"error":null,"meta":{}})");
	std::ostringstream log_output;
	bhp::StructuredLogger logger("http_lexinet_client_test", log_output);
	auto config = test_config(server.base_url());
	config.allow_insecure_http_for_tests = false;
	config.trusted_ca_certificate_pem_for_tests = server.trusted_ca_certificate_pem();
	vocabulary_panel::HttpLexinetClient client(std::move(config), logger);

	const auto result = client.fetch_next_card("", "trusted-tls-verification-test");
	expect(result.has_value() && !result.value().has_value(),
	       "a trusted certificate matching the loopback IP completes the request");
	expect(server.accepted_connection_count() == 1,
	       "the trusted TLS fixture accepted exactly one connection");
	expect(server.tls_handshake_attempted(),
	       "the trusted TLS fixture observed a handshake");
	expect(server.server_error().empty(),
	       "the trusted TLS fixture completed without server error");
}

void test_untrusted_tls_certificate_is_rejected()
{
	using vocabulary_panel::test_fixture::LoopbackHttpServer;
	LoopbackHttpServer server(
		LoopbackHttpServer::Transport::https, 200,
		R"({"data":null,"error":null,"meta":{}})");
	std::ostringstream log_output;
	bhp::StructuredLogger logger("http_lexinet_client_test", log_output);
	auto config = test_config(server.base_url());
	config.allow_insecure_http_for_tests = false;
	vocabulary_panel::HttpLexinetClient client(std::move(config), logger);

	const auto result = client.fetch_next_card("", "tls-verification-test");
	expect(!result.has_value() &&
		       result.error().error_code ==
			       "LEXINET.TLS_CERTIFICATE_VERIFICATION_FAILED" &&
		       !result.error().retryable,
	       "an untrusted certificate returns the typed non-retryable TLS error");
	expect(server.tls_handshake_attempted(),
	       "the TLS fixture observed a handshake before the request failed");
	expect(server.server_error().empty(), "the TLS fixture completed without server error");
}

void test_trusted_certificate_with_mismatched_hostname_is_rejected()
{
	using vocabulary_panel::test_fixture::LoopbackHttpServer;
	LoopbackHttpServer server(
		LoopbackHttpServer::Transport::https, 200,
		R"({"data":null,"error":null,"meta":{}})", {},
		LoopbackHttpServer::TlsCertificateIdentity::localhost_dns);
	std::ostringstream log_output;
	bhp::StructuredLogger logger("http_lexinet_client_test", log_output);
	auto config = test_config(server.base_url());
	config.allow_insecure_http_for_tests = false;
	config.trusted_ca_certificate_pem_for_tests = server.trusted_ca_certificate_pem();
	vocabulary_panel::HttpLexinetClient client(std::move(config), logger);

	const auto result = client.fetch_next_card("", "hostname-verification-test");
	expect(!result.has_value() &&
		       result.error().error_code ==
			       "LEXINET.TLS_CERTIFICATE_VERIFICATION_FAILED" &&
		       !result.error().retryable,
	       "a trusted certificate with the wrong hostname returns the typed TLS error");
	expect(server.tls_handshake_attempted(),
	       "the mismatched-hostname fixture observed a TLS handshake");
	expect(server.server_error().empty(),
	       "the mismatched-hostname fixture completed without server error");
}

void test_redirect_response_is_refused_without_following_location()
{
	using vocabulary_panel::test_fixture::LoopbackHttpServer;
	LoopbackHttpServer redirect_destination(
		LoopbackHttpServer::Transport::http, 200,
		R"({"data":null,"error":null,"meta":{}})");
	LoopbackHttpServer redirect_source(
		LoopbackHttpServer::Transport::http, 302, "{}",
		redirect_destination.base_url() + "/redirect-target");
	std::ostringstream log_output;
	bhp::StructuredLogger logger("http_lexinet_client_test", log_output);
	vocabulary_panel::HttpLexinetClient client(
		test_config(redirect_source.base_url()), logger);

	const auto result = client.fetch_next_card("", "redirect-refusal-test");
	expect(!result.has_value() &&
		       result.error().error_code == "LEXINET.HTTP_STATUS_REJECTED" &&
		       result.error().error_context.at("status") == "302",
	       "a redirect response is a structured rejected-status error");
	expect(redirect_source.accepted_connection_count() == 1,
	       "the adapter contacted the redirect source exactly once");
	expect(redirect_destination.accepted_connection_count() == 0,
	       "the adapter did not follow the redirect location");
}

void test_response_larger_than_configured_limit_is_rejected()
{
	using vocabulary_panel::test_fixture::LoopbackHttpServer;
	LoopbackHttpServer server(LoopbackHttpServer::Transport::http, 200,
				  std::string(65, 'x'));
	std::ostringstream log_output;
	bhp::StructuredLogger logger("http_lexinet_client_test", log_output);
	auto config = test_config(server.base_url());
	config.maximum_response_bytes = 64;
	vocabulary_panel::HttpLexinetClient client(std::move(config), logger);

	const auto result = client.fetch_next_card("", "response-size-limit-test");
	expect(!result.has_value() &&
		       result.error().error_code ==
			       "LEXINET.RESPONSE_SIZE_LIMIT_EXCEEDED" &&
		       result.error().error_context.at("maximum_response_bytes") == "64" &&
		       !result.error().retryable,
	       "an oversized response returns the typed non-retryable size-limit error");
	expect(server.server_error().empty(),
	       "the oversized-response fixture completed without server error");
}

} // namespace

int main()
{
	std::signal(SIGPIPE, SIG_IGN);
	test_trusted_matching_tls_certificate_succeeds();
	test_untrusted_tls_certificate_is_rejected();
	test_trusted_certificate_with_mismatched_hostname_is_rejected();
	test_redirect_response_is_refused_without_following_location();
	test_response_larger_than_configured_limit_is_rejected();
	if (failures != 0)
		return 1;
	std::cout << "http_lexinet_client: " << assertions << " assertions passed\n";
	return 0;
}
