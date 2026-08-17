#include "bhp/panel_mode_protocol/codec.h"
#include "bhp/panel_mode_protocol/framing.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <chrono>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace bhp::panel_mode_protocol {
namespace {

#define TEST_REQUIRE(condition)                                                                    \
	do {                                                                                       \
		if (!(condition)) {                                                                \
			std::cerr << __func__ << ": requirement failed: " #condition << '\n';      \
			return false;                                                              \
		}                                                                                  \
	} while (false)

std::string hex(const std::vector<std::uint8_t> &bytes)
{
	constexpr char kHex[] = "0123456789abcdef";
	std::string result(bytes.size() * 2U, '0');
	for (std::size_t index = 0; index < bytes.size(); ++index) {
		result[index * 2U] = kHex[(bytes[index] >> 4U) & 0x0fU];
		result[index * 2U + 1U] = kHex[bytes[index] & 0x0fU];
	}
	return result;
}

Snapshot vector_a()
{
	return Snapshot{
		"0.1.0+g0123456789abcdef0123456789abcdef01234567",
		"00112233445566778899aabbccddeeff",
		"ffeeddccbbaa99887766554433221100",
		42,
		{},
		DisplayMode::DayActive,
		PresenceState::Present,
		SolarState::Day,
		SystemState::Active,
		88,
		1'700'000'000'123LL,
	};
}

Snapshot vector_b()
{
	return Snapshot{
		"0.1.0-dirty",
		"102132435465768798a9bacbdcedfe0f",
		"0ffedccbbaa998877665544332211020",
		kMaximumSafeJsonInteger,
		{},
		DisplayMode::Suspended,
		PresenceState::Absent,
		SolarState::Night,
		SystemState::Suspended,
		0,
		-1,
	};
}

bool golden_digest_vectors_are_stable()
{
	const auto a_preimage = snapshot_digest_preimage(vector_a());
	TEST_REQUIRE(a_preimage);
	TEST_REQUIRE(a_preimage.value().size() == 130U);
	TEST_REQUIRE(
		hex(a_preimage.value()) ==
		"6268702e70616e656c5f6d6f64652e736e617073686f742e76320002002f302e312e"
		"302b673031323334353637383961626364656630313233343536373839616263646566"
		"303132333435363700112233445566778899aabbccddeeffffeeddccbbaa998877665544"
		"33221100000000000000002a01010101580000018bcfe5687b");
	const auto a_digest = compute_snapshot_id(vector_a());
	TEST_REQUIRE(a_digest);
	TEST_REQUIRE(a_digest.value() ==
		     "sha256:8a1859c8bf700316733bf3f2db1c65947b2cdba77b83dff3410cc115ef3afc9d");

	const auto b_preimage = snapshot_digest_preimage(vector_b());
	TEST_REQUIRE(b_preimage);
	TEST_REQUIRE(b_preimage.value().size() == 94U);
	TEST_REQUIRE(
		hex(b_preimage.value()) ==
		"6268702e70616e656c5f6d6f64652e736e617073686f742e76320002000b302e312e"
		"302d6469727479102132435465768798a9bacbdcedfe0f0ffedccbbaa99887766554433"
		"2211020001fffffffffffff0303020200ffffffffffffffff");
	const auto b_digest = compute_snapshot_id(vector_b());
	TEST_REQUIRE(b_digest);
	TEST_REQUIRE(b_digest.value() ==
		     "sha256:b2dfe86f05894b7c0d2f518685fabe9f01accc7d4ed3a5d9fb57b62f6404b08f");
	return true;
}

bool snapshot_round_trip_and_json_order_independence()
{
	Snapshot snapshot = vector_a();
	const auto digest = compute_snapshot_id(snapshot);
	TEST_REQUIRE(digest);
	snapshot.snapshot_id = digest.value();
	const auto encoded = encode_snapshot(snapshot);
	TEST_REQUIRE(encoded);
	const auto decoded = decode_snapshot(encoded.value());
	TEST_REQUIRE(decoded);
	TEST_REQUIRE(decoded.value().snapshot_id == snapshot.snapshot_id);
	TEST_REQUIRE(decoded.value().observed_at_unix_milliseconds ==
		     snapshot.observed_at_unix_milliseconds);

	std::ostringstream reordered;
	reordered << "{ \"snapshot_id\" : \"" << snapshot.snapshot_id
		  << "\", \"event_sequence\" : 42, \"system_state\" : \"active\","
		     " \"message_type\" : \"lifecycle_snapshot\","
		     " \"presence_state\" : \"present\", \"protocol_version\" : 2,"
		     " \"solar_state\" : \"day\", \"effective_brightness_percent\" : 88,"
		     " \"connection_challenge\" : \"ffeeddccbbaa99887766554433221100\","
		     " \"observed_at_unix_milliseconds\" : 1700000000123,"
		     " \"display_mode\" : \"day_active\","
		     " \"appliance_release_id\" : "
		  << "\"0.1.0+g0123456789abcdef0123456789abcdef01234567\","
		     " \"supervisor_session_id\" : \"00112233445566778899aabbccddeeff\" }\n";
	const auto reordered_decoded = decode_snapshot(reordered.str());
	TEST_REQUIRE(reordered_decoded);
	TEST_REQUIRE(reordered_decoded.value().snapshot_id == snapshot.snapshot_id);
	return true;
}

bool strict_snapshot_failures_are_structured()
{
	const auto encoded = encode_snapshot(vector_a());
	TEST_REQUIRE(encoded);
	std::string duplicate = encoded.value();
	const std::string prefix = "{\"protocol_version\":2,";
	duplicate.replace(0, 1, prefix);
	const auto duplicate_result = decode_snapshot(duplicate);
	TEST_REQUIRE(!duplicate_result);
	TEST_REQUIRE(duplicate_result.error().code == ErrorCode::FieldDuplicate);

	std::string unknown = encoded.value();
	unknown.replace(0, 1, "{\"extra\":1,");
	const auto unknown_result = decode_snapshot(unknown);
	TEST_REQUIRE(!unknown_result);
	TEST_REQUIRE(unknown_result.error().code == ErrorCode::FieldUnknown);
	TEST_REQUIRE(unknown_result.error().context.at("field") == "unknown");
	TEST_REQUIRE(unknown_result.error().context.find("extra") ==
		     unknown_result.error().context.end());

	std::string floating_version = encoded.value();
	const std::string version = "\"protocol_version\":2";
	const auto version_position = floating_version.find(version);
	TEST_REQUIRE(version_position != std::string::npos);
	floating_version.replace(version_position, version.size(),
				 "\"protocol_version\":2.0");
	const auto floating_result = decode_snapshot(floating_version);
	TEST_REQUIRE(!floating_result);
	TEST_REQUIRE(floating_result.error().code == ErrorCode::FieldTypeInvalid);

	std::string mismatched = encoded.value();
	const std::string needle = "\"effective_brightness_percent\":88";
	const auto brightness = mismatched.find(needle);
	TEST_REQUIRE(brightness != std::string::npos);
	mismatched.replace(brightness, needle.size(),
			   "\"effective_brightness_percent\":87");
	const auto mismatch_result = decode_snapshot(mismatched);
	TEST_REQUIRE(!mismatch_result);
	TEST_REQUIRE(mismatch_result.error().code == ErrorCode::SnapshotIdMismatch);

	std::string escaped = encoded.value();
	const auto release = escaped.find("0.1.0+");
	TEST_REQUIRE(release != std::string::npos);
	escaped.replace(release, 2, "0\\u002e");
	const auto escaped_result = decode_snapshot(escaped);
	TEST_REQUIRE(!escaped_result);
	TEST_REQUIRE(escaped_result.error().code == ErrorCode::JsonInvalid);

	std::string no_line_feed = encoded.value();
	no_line_feed.pop_back();
	const auto line_result = decode_snapshot(no_line_feed);
	TEST_REQUIRE(!line_result);
	TEST_REQUIRE(line_result.error().code == ErrorCode::FrameEncodingInvalid);

	std::string too_large(kMaximumFrameBytes, ' ');
	too_large.push_back('\n');
	const auto large_result = decode_snapshot(too_large);
	TEST_REQUIRE(!large_result);
	TEST_REQUIRE(large_result.error().code == ErrorCode::FrameTooLarge);
	return true;
}

bool stream_framing_is_bounded_and_chunk_independent()
{
	FrameAccumulator accumulator;
	const auto partial = accumulator.append("{\"a\":1");
	TEST_REQUIRE(partial && partial.value().empty());
	const auto complete = accumulator.append("}\n{}\n");
	TEST_REQUIRE(complete && complete.value().size() == 2U);
	TEST_REQUIRE(complete.value()[0] == "{\"a\":1}\n");
	TEST_REQUIRE(complete.value()[1] == "{}\n");
	TEST_REQUIRE(accumulator.finish());

	TEST_REQUIRE(accumulator.append("unterminated"));
	const auto truncated = accumulator.finish();
	TEST_REQUIRE(!truncated);
	TEST_REQUIRE(truncated.error().code == ErrorCode::FrameEncodingInvalid);
	accumulator.reset();
	std::string maximum_without_line_feed(kMaximumFrameBytes, 'x');
	TEST_REQUIRE(accumulator.append(maximum_without_line_feed));
	const auto oversized = accumulator.append("\n");
	TEST_REQUIRE(!oversized);
	TEST_REQUIRE(oversized.error().code == ErrorCode::FrameTooLarge);
	return true;
}

bool wall_clock_conversion_floors_and_enforces_bounds()
{
	using Clock = std::chrono::system_clock;
	const Clock::time_point one_negative_tick{Clock::duration{-1}};
	const auto expected =
		std::chrono::floor<std::chrono::milliseconds>(one_negative_tick.time_since_epoch())
			.count();
	const auto converted = observed_at_unix_milliseconds(one_negative_tick);
	TEST_REQUIRE(converted);
	TEST_REQUIRE(converted.value() == expected);
	TEST_REQUIRE(converted.value() < 0);

	const auto minimum = observed_at_unix_milliseconds(Clock::time_point(
		std::chrono::duration_cast<Clock::duration>(std::chrono::milliseconds(
			kMinimumObservedAtUnixMilliseconds))));
	TEST_REQUIRE(minimum);
	const auto exclusive_maximum = observed_at_unix_milliseconds(Clock::time_point(
		std::chrono::duration_cast<Clock::duration>(std::chrono::milliseconds(
			kMaximumObservedAtUnixMillisecondsExclusive))));
	TEST_REQUIRE(!exclusive_maximum);
	TEST_REQUIRE(exclusive_maximum.error().code == ErrorCode::FieldValueInvalid);
	return true;
}

bool acknowledgement_round_trip_is_exact()
{
	const auto digest = compute_snapshot_id(vector_a());
	TEST_REQUIRE(digest);
	const PresentationAcknowledgement acknowledgement{
		"0.1.0+g0123456789abcdef0123456789abcdef01234567",
		"00112233445566778899aabbccddeeff",
		"ffeeddccbbaa99887766554433221100",
		42,
		digest.value(),
		DisplayMode::DayActive,
		7,
		PresentationResult::Committed,
	};
	const auto encoded = encode_presentation_acknowledgement(acknowledgement);
	TEST_REQUIRE(encoded);
	const auto decoded = decode_presentation_acknowledgement(encoded.value());
	TEST_REQUIRE(decoded);
	TEST_REQUIRE(decoded.value().render_generation == 7U);
	TEST_REQUIRE(decoded.value().presentation_result == PresentationResult::Committed);
	TEST_REQUIRE(decoded.value().snapshot_id == digest.value());
	return true;
}

using TestFunction = bool (*)();

} // namespace
} // namespace bhp::panel_mode_protocol

int main()
{
	using namespace bhp::panel_mode_protocol;
	const std::array<std::pair<std::string_view, TestFunction>, 6> tests{{
		{"golden digests", golden_digest_vectors_are_stable},
		{"snapshot round trip", snapshot_round_trip_and_json_order_independence},
		{"strict failures", strict_snapshot_failures_are_structured},
		{"acknowledgement round trip", acknowledgement_round_trip_is_exact},
		{"stream framing", stream_framing_is_bounded_and_chunk_independent},
		{"wall clock conversion", wall_clock_conversion_floors_and_enforces_bounds},
	}};
	for (const auto &[name, test] : tests) {
		if (!test()) {
			std::cerr << "FAILED: " << name << '\n';
			return 1;
		}
	}
	std::cout << "panel_mode_protocol_test: " << tests.size() << " passed\n";
	return 0;
}
