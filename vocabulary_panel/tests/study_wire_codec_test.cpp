#include "vocabulary_panel/study_wire_codec.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
using namespace vocabulary_panel;
int main()
{
	int failures = 0, checks = 0;
	auto check = [&](bool v, const char *name) {
		++checks;
		if (!v) {
			++failures;
			std::cerr << name << '\n';
		}
	};
	auto fixture = [](const std::string &name) {
		std::ifstream f(std::filesystem::path(__FILE__).parent_path() /
				"fixtures/study_v1/examples" / name);
		return std::string(std::istreambuf_iterator<char>(f), {});
	};
	for (const auto *name : {"session-created.json", "session-active-page-exhausted.json",
				 "session-waiting-for-corpus.json"})
		check(decode_study_session(fixture(name)).has_value(), name);
	for (const auto *name : {"sync-rated-and-voided.json", "sync-new-suffix-mixed-success.json",
				 "sync-replay-prefix-then-grouped-terminal-suffix.json"})
		check(decode_study_sync(fixture(name)).has_value(), name);
	auto original = nlohmann::json::parse(fixture("session-created.json"));
	auto changed = original;
	changed["protocol_version"] = "study.v2";
	check(!decode_study_session(changed.dump()).has_value(), "unknown version rejected");
	changed = original;
	changed["scheduler"].erase("seed");
	check(!decode_study_session(changed.dump()).has_value(),
	      "missing scheduler provenance rejected");
	changed = original;
	changed["page"]["items"][0]["position"] = 2;
	check(!decode_study_session(changed.dump()).has_value(), "out-of-order page rejected");
	changed = original;
	changed["page"]["items"][0]["source_revision"]["source_identity"] = "not-uuid";
	check(!decode_study_session(changed.dump()).has_value(),
	      "invalid source identity rejected");
	changed = original;
	changed["future_optional_field"] = true;
	check(decode_study_session(changed.dump()).has_value(), "optional extension permitted");
	check(!decode_study_session("{truncated").has_value(), "truncated JSON rejected");
	auto malformed = fixture("session-created.json");
	malformed.insert(malformed.find("Fixture Concept"), 1, static_cast<char>(0xff));
	check(!decode_study_session(malformed).has_value(), "malformed UTF-8 rejected");
	check(!study_timestamp_seconds("2026-02-30T12:00:00Z").has_value(),
	      "invalid calendar date rejected");
	check(decode_study_error(fixture("error-idempotency-conflict.json"), 409).error_code ==
		  "IDEMPOTENCY_CONFLICT",
	      "stable structured error preserved");
	check(decode_study_error("{}", 503).error_code == "LEXINET.RESPONSE_SCHEMA_INVALID",
	      "incomplete errors not accepted as retry success");
	StudyCreateRequest r;
	r.creation_request_id = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
	r.profile_ref = "fixture-default";
	r.topic_refs = {"fixture-systems", "fixture-words"};
	auto encoded = encode_study_create(r);
	check(encoded.has_value() &&
		  nlohmann::json::parse(encoded.value()).at("page").at("limit") == 64,
	      "typed create request exact schema");
	r.topic_refs = {"z", "a"};
	check(!encode_study_create(r).has_value(), "unnormalized corpus selector rejected");
	std::cout << "study_wire_codec: " << checks << " checks, " << failures << " failures\n";
	return failures ? 1 : 0;
}
