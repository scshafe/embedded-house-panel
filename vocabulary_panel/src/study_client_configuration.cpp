#include "vocabulary_panel/study_client_configuration.h"
#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <utility>
namespace vocabulary_panel
{
namespace
{
using Result = VocabularyPanelResult<StudyClientConfiguration>;
constexpr std::string_view kPrefix = "BHP_VOCABULARY_PANEL_";
constexpr std::size_t kMaximumFileBytes = 65536;
constexpr std::size_t kMaximumTopics = 32;
constexpr std::size_t kMaximumReferenceBytes = 128;
const std::set<std::string> kKeys{
    "BHP_VOCABULARY_PANEL_LEXINET_BASE_URL",
    "BHP_VOCABULARY_PANEL_STUDY_PROFILE_REF",
    "BHP_VOCABULARY_PANEL_STUDY_TOPIC_REFS",
    "BHP_VOCABULARY_PANEL_STUDY_AUTH_MODE",
    "BHP_VOCABULARY_PANEL_CREDENTIAL_FILE",
    "BHP_VOCABULARY_PANEL_CONNECT_TIMEOUT_MS",
    "BHP_VOCABULARY_PANEL_REQUEST_TIMEOUT_MS",
    "BHP_VOCABULARY_PANEL_MAXIMUM_RESPONSE_BYTES",
    "BHP_VOCABULARY_PANEL_MAXIMUM_SNAPSHOT_AGE_SECONDS",
};

Result failure(const std::string &code, std::string message, const std::string &key = {})
{
	std::map<std::string, std::string> context{
	    {"operation", "vocabulary_panel.read_study_client_configuration"}};
	if (!key.empty())
		context.emplace("key", key);
	return Result::failure({"STUDY_CONFIG." + code, std::move(message), std::move(context),
				false});
}

bool key_character(const char c)
{
	return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

bool alphanumeric(const char c)
{
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
}

/** study.v1 ProfileRef and TopicRef: ^[A-Za-z0-9][A-Za-z0-9._:-]*$, 1..128. */
bool valid_reference(const std::string_view value)
{
	if (value.empty() || value.size() > kMaximumReferenceBytes || !alphanumeric(value.front()))
		return false;
	return std::all_of(value.begin(), value.end(), [](const char c) {
		return alphanumeric(c) || c == '.' || c == '_' || c == ':' || c == '-';
	});
}

bool placeholder(const std::string_view value)
{
	std::string lower(value);
	std::transform(lower.begin(), lower.end(), lower.begin(),
		       [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return lower.find("replace") != std::string::npos ||
	       lower.find("placeholder") != std::string::npos;
}

/** https://HOST[:PORT] with DNS-style labels; no path, query, fragment or userinfo. */
bool valid_base_url(const std::string_view url)
{
	constexpr std::string_view scheme = "https://";
	if (url.substr(0, scheme.size()) != scheme)
		return false;
	auto authority = url.substr(scheme.size());
	std::string_view host = authority;
	if (const auto colon = authority.find(':'); colon != std::string_view::npos) {
		host = authority.substr(0, colon);
		const auto port = authority.substr(colon + 1);
		unsigned value = 0;
		const auto parsed = std::from_chars(port.data(), port.data() + port.size(), value);
		if (port.empty() || parsed.ec != std::errc{} ||
		    parsed.ptr != port.data() + port.size() || value == 0 || value > 65535)
			return false;
	}
	if (host.empty() || host.size() > 253 || host.front() == '.' || host.back() == '.' ||
	    host.find("..") != std::string_view::npos)
		return false;
	return std::all_of(host.begin(), host.end(),
			   [](const char c) { return alphanumeric(c) || c == '-' || c == '.'; });
}

bool parse_bounded(const std::string_view text, const std::uint64_t maximum,
		   std::uint64_t &value)
{
	const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
	return !text.empty() && parsed.ec == std::errc{} &&
	       parsed.ptr == text.data() + text.size() && value > 0 && value <= maximum;
}
} // namespace

Result parse_study_client_configuration(const std::string_view text)
{
	if (text.size() > kMaximumFileBytes)
		return failure("FILE_TOO_LARGE", "Study configuration exceeds 64 KiB");
	std::map<std::string, std::string> values;
	std::size_t start = 0;
	while (start <= text.size()) {
		auto end = text.find('\n', start);
		if (end == std::string_view::npos)
			end = text.size();
		auto line = text.substr(start, end - start);
		start = end + 1;
		if (!line.empty() && line.back() == '\r')
			line.remove_suffix(1);
		if (line.empty() || line.front() == '#')
			continue;
		const auto equals = line.find('=');
		const auto key = line.substr(0, equals);
		if (equals == std::string_view::npos || key.empty() ||
		    !std::all_of(key.begin(), key.end(), key_character))
			return failure("SYNTAX_INVALID",
				       "Every non-comment line must be KEY=VALUE with an upper-case key");
		if (key.substr(0, kPrefix.size()) != kPrefix)
			continue;
		const std::string name(key);
		if (!kKeys.count(name))
			return failure("KEY_UNKNOWN", "Unknown study configuration key", name);
		const auto value = line.substr(equals + 1);
		if (std::any_of(value.begin(), value.end(), [](const unsigned char c) {
			    return c <= ' ' || c == '"' || c == '\'' || c == '$' || c == '\\' ||
				   c == '`' || c >= 0x7f;
		    }))
			return failure("VALUE_INVALID",
				       "Study values are unquoted printable ASCII without spaces", name);
		if (!values.emplace(name, std::string(value)).second)
			return failure("KEY_DUPLICATE", "Study configuration key appears twice", name);
	}
	for (const auto &key : kKeys)
		if (!values.count(key))
			return failure("KEY_MISSING", "Study configuration key is required", key);

	StudyClientConfiguration configuration;
	const auto &url = values.at("BHP_VOCABULARY_PANEL_LEXINET_BASE_URL");
	if (placeholder(url))
		return failure("PLACEHOLDER", "Study value is still a placeholder",
			       "BHP_VOCABULARY_PANEL_LEXINET_BASE_URL");
	if (!valid_base_url(url))
		return failure("BASE_URL_INVALID", "Lexinet base URL must be https://HOST[:PORT]",
			       "BHP_VOCABULARY_PANEL_LEXINET_BASE_URL");
	configuration.http.lexinet_base_url = url;

	const auto &profile = values.at("BHP_VOCABULARY_PANEL_STUDY_PROFILE_REF");
	if (placeholder(profile))
		return failure("PLACEHOLDER", "Study value is still a placeholder",
			       "BHP_VOCABULARY_PANEL_STUDY_PROFILE_REF");
	if (!valid_reference(profile))
		return failure("PROFILE_REF_INVALID", "Profile ref fails the study.v1 pattern",
			       "BHP_VOCABULARY_PANEL_STUDY_PROFILE_REF");
	configuration.profile_ref = profile;

	const std::string topics_key = "BHP_VOCABULARY_PANEL_STUDY_TOPIC_REFS";
	const std::string_view topics = values.at(topics_key);
	if (placeholder(topics))
		return failure("PLACEHOLDER", "Study value is still a placeholder", topics_key);
	std::set<std::string> topic_set;
	std::size_t topic_count = 0;
	for (std::size_t from = 0; from <= topics.size();) {
		auto comma = topics.find(',', from);
		if (comma == std::string_view::npos)
			comma = topics.size();
		const auto topic = topics.substr(from, comma - from);
		if (!valid_reference(topic))
			return failure("TOPIC_REFS_INVALID",
				       "Each topic ref must match the study.v1 pattern", topics_key);
		topic_set.emplace(topic);
		++topic_count;
		from = comma + 1;
	}
	if (topic_count > kMaximumTopics || topic_set.size() != topic_count)
		return failure("TOPIC_REFS_INVALID", "Topic refs must be 1 to 32 unique values",
			       topics_key);
	configuration.topic_refs.assign(topic_set.begin(), topic_set.end());

	if (values.at("BHP_VOCABULARY_PANEL_STUDY_AUTH_MODE") != "tailnet_node")
		return failure("AUTH_MODE_UNSUPPORTED",
			       "ADR 0010: the panel authenticates as its tagged tailnet node",
			       "BHP_VOCABULARY_PANEL_STUDY_AUTH_MODE");
	if (!values.at("BHP_VOCABULARY_PANEL_CREDENTIAL_FILE").empty())
		return failure("CREDENTIAL_NOT_ALLOWED",
			       "tailnet_node mode stores no study credential",
			       "BHP_VOCABULARY_PANEL_CREDENTIAL_FILE");

	std::uint64_t connect = 0, request = 0, response = 0, age = 0;
	if (!parse_bounded(values.at("BHP_VOCABULARY_PANEL_CONNECT_TIMEOUT_MS"), 60'000U, connect))
		return failure("BOUND_INVALID", "Connect timeout must be 1 to 60000 ms",
			       "BHP_VOCABULARY_PANEL_CONNECT_TIMEOUT_MS");
	if (!parse_bounded(values.at("BHP_VOCABULARY_PANEL_REQUEST_TIMEOUT_MS"), 600'000U, request) ||
	    request < connect)
		return failure("BOUND_INVALID",
			       "Request timeout must be 1 to 600000 ms and at least the connect "
			       "timeout",
			       "BHP_VOCABULARY_PANEL_REQUEST_TIMEOUT_MS");
	if (!parse_bounded(values.at("BHP_VOCABULARY_PANEL_MAXIMUM_RESPONSE_BYTES"), 1'048'576U,
			   response))
		return failure("BOUND_INVALID",
			       "Response ceiling must be 1 to 1048576 bytes (the study.v1 limit)",
			       "BHP_VOCABULARY_PANEL_MAXIMUM_RESPONSE_BYTES");
	if (!parse_bounded(values.at("BHP_VOCABULARY_PANEL_MAXIMUM_SNAPSHOT_AGE_SECONDS"),
			   2'592'000U, age))
		return failure("BOUND_INVALID", "Snapshot age must be 1 to 2592000 seconds",
			       "BHP_VOCABULARY_PANEL_MAXIMUM_SNAPSHOT_AGE_SECONDS");
	configuration.http.transport_budget = {static_cast<long>(connect),
					       static_cast<long>(request),
					       static_cast<std::size_t>(response)};
	configuration.http.maximum_snapshot_age_seconds = age;
	configuration.http.clock_trusted = false;
	return Result::success(std::move(configuration));
}

Result read_study_client_configuration(const std::string &path)
{
	if (path.empty() || path.front() != '/')
		return failure("PATH_INVALID", "Study configuration path must be absolute");
	std::ifstream file(path, std::ios::binary);
	if (!file)
		return failure("READ_FAILED", "Study configuration file is unreadable");
	std::string text;
	text.resize(kMaximumFileBytes + 1);
	file.read(text.data(), static_cast<std::streamsize>(text.size()));
	text.resize(static_cast<std::size_t>(file.gcount()));
	if (file.bad())
		return failure("READ_FAILED", "Study configuration file is unreadable");
	return parse_study_client_configuration(text);
}
} // namespace vocabulary_panel
