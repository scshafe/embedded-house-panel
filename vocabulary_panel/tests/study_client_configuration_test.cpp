#include "vocabulary_panel/study_client_configuration.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
using namespace vocabulary_panel;
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
const std::string kValid = "# comment\n"
			   "BHP_PANEL_UI_ENABLED=0\n"
			   "\n"
			   "BHP_VOCABULARY_PANEL_LEXINET_BASE_URL=https://lexinet-study.tail1234.ts.net\n"
			   "BHP_VOCABULARY_PANEL_STUDY_PROFILE_REF=cole\n"
			   "BHP_VOCABULARY_PANEL_STUDY_TOPIC_REFS=words,systems\n"
			   "BHP_VOCABULARY_PANEL_STUDY_AUTH_MODE=tailnet_node\n"
			   "BHP_VOCABULARY_PANEL_CREDENTIAL_FILE=\n"
			   "BHP_VOCABULARY_PANEL_CONNECT_TIMEOUT_MS=1000\n"
			   "BHP_VOCABULARY_PANEL_REQUEST_TIMEOUT_MS=3000\n"
			   "BHP_VOCABULARY_PANEL_MAXIMUM_RESPONSE_BYTES=1048576\n"
			   "BHP_VOCABULARY_PANEL_MAXIMUM_SNAPSHOT_AGE_SECONDS=604800\n";

std::string replaced(const std::string &key, const std::string &value)
{
	auto text = kValid;
	const auto at = text.find(key + "=");
	const auto end = text.find('\n', at);
	return text.replace(at, end - at, key + "=" + value);
}

void expect_failure(const std::string &text, const std::string &code, const std::string &key,
		    const std::string &label)
{
	const auto result = parse_study_client_configuration(text);
	const bool matches = !result.has_value() && result.error().error_code == code &&
			     (key.empty() || (result.error().error_context.count("key") &&
						  result.error().error_context.at("key") == key));
	expect(matches, label + (result.has_value() ? " (accepted)" : " (" + result.error().error_code + ")"));
	if (!result.has_value())
		expect(result.error().error_message.find("tail1234") == std::string::npos &&
			   !result.error().retryable,
		       label + ": no value in the error, not retryable");
}

void test_valid_file_maps_every_value()
{
	const auto result = parse_study_client_configuration(kValid);
	expect(result.has_value(), "a filled-in file is accepted");
	if (!result.has_value())
		return;
	const auto &c = result.value();
	expect(c.http.lexinet_base_url == "https://lexinet-study.tail1234.ts.net", "base URL");
	expect(c.profile_ref == "cole", "profile ref");
	expect(c.topic_refs == std::vector<std::string>{"systems", "words"}, "topics sorted");
	expect(c.http.credential_file_path.empty(), "no credential");
	expect(c.http.transport_budget.connect_timeout_ms == 1000 &&
		   c.http.transport_budget.request_timeout_ms == 3000 &&
		   c.http.transport_budget.maximum_response_bytes == 1048576,
	       "transport budget");
	expect(c.http.maximum_snapshot_age_seconds == 604800, "snapshot age");
	expect(!c.http.clock_trusted && !c.http.allow_insecure_http_for_tests &&
		   !c.http.allow_qemu_host_test_ca &&
		   c.http.trusted_ca_certificate_pem_for_tests.empty(),
	       "production transport policy");
	auto crlf = kValid;
	for (std::size_t at = crlf.find('\n'); at != std::string::npos; at = crlf.find('\n', at + 2))
		crlf.insert(at, "\r");
	expect(parse_study_client_configuration(crlf).has_value(), "CRLF lines are accepted");
	expect(parse_study_client_configuration(
		   replaced("BHP_VOCABULARY_PANEL_LEXINET_BASE_URL", "https://lexinet.example:8443"))
		   .has_value(),
	       "an explicit port is accepted");
}

void test_the_checked_in_example_is_rejected_for_its_placeholders()
{
	std::ifstream file(std::string(BHP_SOURCE_ROOT) + "/config/vocabulary-panel.env.example");
	const std::string text((std::istreambuf_iterator<char>(file)), {});
	expect(!text.empty(), "the example is readable");
	expect_failure(text, "STUDY_CONFIG.PLACEHOLDER", "", "the example is rejected");
}

void test_structure()
{
	const std::string url = "BHP_VOCABULARY_PANEL_LEXINET_BASE_URL";
	expect_failure(kValid + "BHP_VOCABULARY_PANEL_EXTRA=1\n", "STUDY_CONFIG.KEY_UNKNOWN",
		       "BHP_VOCABULARY_PANEL_EXTRA", "unknown study key");
	expect_failure(kValid + url + "=https://other.ts.net\n", "STUDY_CONFIG.KEY_DUPLICATE", url,
		       "duplicate key");
	auto missing = kValid;
	missing.erase(missing.find("BHP_VOCABULARY_PANEL_STUDY_AUTH_MODE"),
		      std::string("BHP_VOCABULARY_PANEL_STUDY_AUTH_MODE=tailnet_node\n").size());
	expect_failure(missing, "STUDY_CONFIG.KEY_MISSING", "BHP_VOCABULARY_PANEL_STUDY_AUTH_MODE",
		       "missing key");
	expect_failure(kValid + "export FOO=1\n", "STUDY_CONFIG.SYNTAX_INVALID", "",
		       "a shell export line");
	expect_failure(kValid + "not a pair\n", "STUDY_CONFIG.SYNTAX_INVALID", "", "a bare line");
	expect_failure(replaced("BHP_VOCABULARY_PANEL_STUDY_PROFILE_REF", "\"cole\""),
		       "STUDY_CONFIG.VALUE_INVALID", "BHP_VOCABULARY_PANEL_STUDY_PROFILE_REF",
		       "a quoted value");
	expect_failure(replaced("BHP_VOCABULARY_PANEL_STUDY_PROFILE_REF", "$PROFILE"),
		       "STUDY_CONFIG.VALUE_INVALID", "BHP_VOCABULARY_PANEL_STUDY_PROFILE_REF",
		       "a shell expansion");
	expect_failure(std::string(65537, '#'), "STUDY_CONFIG.FILE_TOO_LARGE", "",
		       "an oversized file");
}

void test_values()
{
	const std::string url = "BHP_VOCABULARY_PANEL_LEXINET_BASE_URL";
	for (const char *bad : {"http://lexinet.ts.net", "https://", "https://lexinet.ts.net/api",
				"https://user@lexinet.ts.net", "https://lexinet.ts.net?x=1",
				"https://lexinet.ts.net:0", "https://lexinet.ts.net:70000",
				"https://lexinet..ts.net"})
		expect_failure(replaced(url, bad), "STUDY_CONFIG.BASE_URL_INVALID", url,
			       std::string("base URL ") + bad);
	expect_failure(replaced(url, "https://lexinet-placeholder.example.ts.net"),
		       "STUDY_CONFIG.PLACEHOLDER", url, "placeholder URL");
	const std::string profile = "BHP_VOCABULARY_PANEL_STUDY_PROFILE_REF";
	expect_failure(replaced(profile, "replace.with.profile"), "STUDY_CONFIG.PLACEHOLDER", profile,
		       "placeholder profile");
	expect_failure(replaced(profile, "-cole"), "STUDY_CONFIG.PROFILE_REF_INVALID", profile,
		       "profile pattern");
	expect_failure(replaced(profile, std::string(129, 'a')), "STUDY_CONFIG.PROFILE_REF_INVALID",
		       profile, "profile length");
	const std::string topics = "BHP_VOCABULARY_PANEL_STUDY_TOPIC_REFS";
	for (const char *bad : {"", "words,", ",words", "words,,systems", "wo rds"}) {
		const auto result = parse_study_client_configuration(replaced(topics, bad));
		expect(!result.has_value(), std::string("topics rejected: '") + bad + "'");
	}
	expect_failure(replaced(topics, "words,words"), "STUDY_CONFIG.TOPIC_REFS_INVALID", topics,
		       "duplicate topic");
	std::string many;
	for (int index = 0; index < 33; ++index)
		many += (index ? ",t" : "t") + std::to_string(index);
	expect_failure(replaced(topics, many), "STUDY_CONFIG.TOPIC_REFS_INVALID", topics,
		       "33 topics");
	expect_failure(replaced("BHP_VOCABULARY_PANEL_STUDY_AUTH_MODE", "scoped_token"),
		       "STUDY_CONFIG.AUTH_MODE_UNSUPPORTED", "BHP_VOCABULARY_PANEL_STUDY_AUTH_MODE",
		       "auth mode");
	expect_failure(replaced("BHP_VOCABULARY_PANEL_CREDENTIAL_FILE", "/etc/study.token"),
		       "STUDY_CONFIG.CREDENTIAL_NOT_ALLOWED", "BHP_VOCABULARY_PANEL_CREDENTIAL_FILE",
		       "stored credential");
	const std::string connect = "BHP_VOCABULARY_PANEL_CONNECT_TIMEOUT_MS";
	const std::string request = "BHP_VOCABULARY_PANEL_REQUEST_TIMEOUT_MS";
	for (const char *bad : {"0", "-1", "60001", "1e3", "1000ms"})
		expect_failure(replaced(connect, bad), "STUDY_CONFIG.BOUND_INVALID", connect,
			       std::string("connect ") + bad);
	expect_failure(replaced(request, "999"), "STUDY_CONFIG.BOUND_INVALID", request,
		       "request shorter than connect");
	expect_failure(replaced("BHP_VOCABULARY_PANEL_MAXIMUM_RESPONSE_BYTES", "1048577"),
		       "STUDY_CONFIG.BOUND_INVALID", "BHP_VOCABULARY_PANEL_MAXIMUM_RESPONSE_BYTES",
		       "response above the contract limit");
	expect_failure(replaced("BHP_VOCABULARY_PANEL_MAXIMUM_SNAPSHOT_AGE_SECONDS", "0"),
		       "STUDY_CONFIG.BOUND_INVALID",
		       "BHP_VOCABULARY_PANEL_MAXIMUM_SNAPSHOT_AGE_SECONDS", "zero snapshot age");
}

void test_read_requires_an_absolute_readable_path()
{
	const auto relative = read_study_client_configuration("config/vocabulary-panel.env");
	expect(!relative.has_value() && relative.error().error_code == "STUDY_CONFIG.PATH_INVALID",
	       "relative path");
	const auto missing = read_study_client_configuration("/nonexistent/vocabulary-panel.env");
	expect(!missing.has_value() && missing.error().error_code == "STUDY_CONFIG.READ_FAILED",
	       "missing file");
}
} // namespace

int main()
{
	test_valid_file_maps_every_value();
	test_the_checked_in_example_is_rejected_for_its_placeholders();
	test_structure();
	test_values();
	test_read_requires_an_absolute_readable_path();
	std::cout << "study_client_configuration: " << failures << " failures\n";
	return failures ? 1 : 0;
}
