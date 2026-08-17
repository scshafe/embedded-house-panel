#include "bhp/panel_mode_protocol/codec.h"

#include <openssl/evp.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <locale>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace bhp::panel_mode_protocol {
namespace {

constexpr std::string_view kDigestDomain = "bhp.panel_mode.snapshot.v2";
constexpr std::string_view kSnapshotMessageType = "lifecycle_snapshot";
constexpr std::string_view kAcknowledgementMessageType =
	"presentation_acknowledgement";

enum class JsonValueKind { String, Integer };

struct JsonValue {
	JsonValueKind kind;
	std::string text;
};

using JsonObject = std::map<std::string, JsonValue>;

Error make_error(const ErrorCode code, std::string message,
		 std::map<std::string, std::string> context = {})
{
	return Error{code, std::move(message), std::move(context)};
}

Result<JsonObject> frame_error(const ErrorCode code, const std::string &message,
			       const std::string &operation)
{
	return Result<JsonObject>::failure(
		make_error(code, message, {{"operation", operation}}));
}

bool is_json_space(const char value) noexcept { return value == ' ' || value == '\t'; }

class FlatJsonParser final {
public:
	FlatJsonParser(const std::string_view input, const std::set<std::string> &allowed_fields,
		       std::string operation)
		: input_(input), allowed_fields_(allowed_fields), operation_(std::move(operation))
	{
	}

	[[nodiscard]] Result<JsonObject> parse()
	{
		skip_space();
		if (!consume('{'))
			return invalid_json("Expected one JSON object");
		skip_space();
		JsonObject values;
		if (consume('}')) {
			skip_space();
			return at_end() ? Result<JsonObject>::success(std::move(values))
					: invalid_json("Unexpected bytes after JSON object");
		}

		while (true) {
			auto key_result = parse_string();
			if (!key_result)
				return Result<JsonObject>::failure(key_result.error());
			const std::string key = std::move(key_result).value();
			if (allowed_fields_.find(key) == allowed_fields_.end()) {
				return Result<JsonObject>::failure(make_error(
					ErrorCode::FieldUnknown, "Protocol frame contains an unknown field",
					{{"field", "unknown"}, {"operation", operation_}}));
			}
			if (values.find(key) != values.end()) {
				return Result<JsonObject>::failure(make_error(
					ErrorCode::FieldDuplicate,
					"Protocol frame contains a duplicate field",
					{{"field", key}, {"operation", operation_}}));
			}
			skip_space();
			if (!consume(':'))
				return invalid_json("Expected a colon after a JSON member name");
			skip_space();

			JsonValue value{JsonValueKind::Integer, {}};
			if (peek() == '"') {
				auto string_result = parse_string();
				if (!string_result)
					return Result<JsonObject>::failure(string_result.error());
				value = JsonValue{JsonValueKind::String,
						  std::move(string_result).value()};
			} else if (peek() == '-' || is_digit(peek())) {
				auto integer_result = parse_integer_token();
				if (!integer_result)
					return Result<JsonObject>::failure(integer_result.error());
				value = JsonValue{JsonValueKind::Integer,
						  std::move(integer_result).value()};
			} else {
				return Result<JsonObject>::failure(make_error(
					ErrorCode::FieldTypeInvalid,
					"Protocol field must be a JSON string or integer",
					{{"field", key}, {"operation", operation_}}));
			}
			values.emplace(key, std::move(value));

			skip_space();
			if (consume('}')) {
				skip_space();
				if (!at_end())
					return invalid_json("Unexpected bytes after JSON object");
				return Result<JsonObject>::success(std::move(values));
			}
			if (!consume(','))
				return invalid_json("Expected a comma between JSON members");
			skip_space();
		}
	}

private:
	[[nodiscard]] Result<JsonObject> invalid_json(const std::string &message) const
	{
		return Result<JsonObject>::failure(make_error(
			ErrorCode::JsonInvalid, message, {{"operation", operation_}}));
	}

	[[nodiscard]] Result<std::string> invalid_string(const std::string &message) const
	{
		return Result<std::string>::failure(make_error(
			ErrorCode::JsonInvalid, message, {{"operation", operation_}}));
	}

	[[nodiscard]] Result<std::string> parse_string()
	{
		if (!consume('"'))
			return invalid_string("Expected a JSON string");
		const std::size_t start = position_;
		while (!at_end() && peek() != '"') {
			const unsigned char value =
				static_cast<unsigned char>(input_[position_]);
			if (value == '\\') {
				return invalid_string(
					"Protocol JSON strings do not permit escape sequences");
			}
			if (value < 0x20U || value > 0x7eU) {
				return Result<std::string>::failure(make_error(
					ErrorCode::FrameEncodingInvalid,
					"Protocol JSON strings must contain printable ASCII",
					{{"operation", operation_}}));
			}
			++position_;
		}
		if (!consume('"'))
			return invalid_string("Unterminated JSON string");
		return Result<std::string>::success(
			std::string(input_.substr(start, position_ - start - 1U)));
	}

	[[nodiscard]] Result<std::string> parse_integer_token()
	{
		const std::size_t start = position_;
		if (peek() == '-')
			++position_;
		if (at_end() || !is_digit(peek())) {
			return Result<std::string>::failure(make_error(
				ErrorCode::JsonInvalid, "Invalid JSON integer",
				{{"operation", operation_}}));
		}
		if (peek() == '0') {
			++position_;
			if (!at_end() && is_digit(peek())) {
				return Result<std::string>::failure(make_error(
					ErrorCode::JsonInvalid,
					"JSON integers cannot contain leading zeros",
					{{"operation", operation_}}));
			}
		} else {
			while (!at_end() && is_digit(peek()))
				++position_;
		}
		const char trailing = peek();
		if (trailing == '.' || trailing == 'e' || trailing == 'E') {
			return Result<std::string>::failure(make_error(
				ErrorCode::FieldTypeInvalid,
				"Protocol numeric fields must be JSON integers",
				{{"operation", operation_}}));
		}
		return Result<std::string>::success(
			std::string(input_.substr(start, position_ - start)));
	}

	void skip_space() noexcept
	{
		while (!at_end() && is_json_space(input_[position_]))
			++position_;
	}

	[[nodiscard]] bool consume(const char expected) noexcept
	{
		if (at_end() || input_[position_] != expected)
			return false;
		++position_;
		return true;
	}

	[[nodiscard]] char peek() const noexcept
	{
		return at_end() ? '\0' : input_[position_];
	}

	[[nodiscard]] bool at_end() const noexcept { return position_ == input_.size(); }
	[[nodiscard]] static bool is_digit(const char value) noexcept
	{
		return value >= '0' && value <= '9';
	}

	std::string_view input_;
	const std::set<std::string> &allowed_fields_;
	std::string operation_;
	std::size_t position_{0};
};

Result<JsonObject> parse_frame(const std::string_view frame,
			       const std::set<std::string> &allowed_fields,
			       const std::string &operation)
{
	if (frame.size() > kMaximumFrameBytes) {
		return frame_error(ErrorCode::FrameTooLarge,
				   "Protocol frame exceeds the maximum byte count", operation);
	}
	if (frame.size() < 3U || frame.back() != '\n') {
		return frame_error(ErrorCode::FrameEncodingInvalid,
				   "Protocol frame must end with one line feed", operation);
	}
	for (std::size_t index = 0; index + 1U < frame.size(); ++index) {
		const unsigned char value = static_cast<unsigned char>(frame[index]);
		if (value == '\r' || value == '\n' || value == '\0' || value > 0x7eU ||
		    (value < 0x20U && value != '\t')) {
			return frame_error(ErrorCode::FrameEncodingInvalid,
					   "Protocol frame contains a forbidden byte", operation);
		}
	}
	return FlatJsonParser(frame.substr(0, frame.size() - 1U), allowed_fields, operation)
		.parse();
}

Result<const JsonValue *> require_field(const JsonObject &object, const std::string &field,
					const JsonValueKind kind,
					const std::string &operation)
{
	const auto iterator = object.find(field);
	if (iterator == object.end()) {
		return Result<const JsonValue *>::failure(make_error(
			ErrorCode::FieldMissing, "Protocol frame is missing a required field",
			{{"field", field}, {"operation", operation}}));
	}
	if (iterator->second.kind != kind) {
		return Result<const JsonValue *>::failure(make_error(
			ErrorCode::FieldTypeInvalid, "Protocol field has the wrong JSON type",
			{{"field", field}, {"operation", operation}}));
	}
	return Result<const JsonValue *>::success(&iterator->second);
}

Result<std::string> require_string(const JsonObject &object, const std::string &field,
				   const std::string &operation)
{
	const auto result = require_field(object, field, JsonValueKind::String, operation);
	if (!result)
		return Result<std::string>::failure(result.error());
	return Result<std::string>::success(result.value()->text);
}

Result<std::uint64_t> require_uint64(const JsonObject &object, const std::string &field,
				     const std::string &operation)
{
	const auto result = require_field(object, field, JsonValueKind::Integer, operation);
	if (!result)
		return Result<std::uint64_t>::failure(result.error());
	const std::string &text = result.value()->text;
	if (text.empty() || text.front() == '-') {
		return Result<std::uint64_t>::failure(make_error(
			ErrorCode::FieldValueInvalid, "Protocol integer must be non-negative",
			{{"field", field}, {"operation", operation}}));
	}
	std::uint64_t value = 0;
	const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
	if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
		return Result<std::uint64_t>::failure(make_error(
			ErrorCode::FieldValueInvalid, "Protocol integer is out of range",
			{{"field", field}, {"operation", operation}}));
	}
	return Result<std::uint64_t>::success(value);
}

Result<std::int64_t> require_int64(const JsonObject &object, const std::string &field,
				   const std::string &operation)
{
	const auto result = require_field(object, field, JsonValueKind::Integer, operation);
	if (!result)
		return Result<std::int64_t>::failure(result.error());
	const std::string &text = result.value()->text;
	std::int64_t value = 0;
	const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
	if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
		return Result<std::int64_t>::failure(make_error(
			ErrorCode::FieldValueInvalid, "Protocol integer is out of range",
			{{"field", field}, {"operation", operation}}));
	}
	return Result<std::int64_t>::success(value);
}

bool is_release_id(const std::string_view value) noexcept
{
	auto is_ascii_alphanumeric = [](const char character) noexcept {
		return (character >= 'A' && character <= 'Z') ||
		       (character >= 'a' && character <= 'z') ||
		       (character >= '0' && character <= '9');
	};
	if (value.empty() || value.size() > 128U || !is_ascii_alphanumeric(value.front()))
		return false;
	return std::all_of(value.begin() + 1, value.end(), [is_ascii_alphanumeric](
							 const char character) {
		return is_ascii_alphanumeric(character) || character == '.' || character == '_' ||
		       character == '+' || character == '-';
	});
}

bool is_lower_hex(const std::string_view value, const std::size_t expected_size) noexcept
{
	return value.size() == expected_size &&
	       std::all_of(value.begin(), value.end(), [](const char character) {
		       return (character >= '0' && character <= '9') ||
			      (character >= 'a' && character <= 'f');
	       });
}

bool is_nonzero_hex(const std::string_view value) noexcept
{
	return std::any_of(value.begin(), value.end(), [](const char character) {
		return character != '0';
	});
}

Result<std::array<std::uint8_t, 16>> decode_identity_hex(const std::string_view value,
							 const std::string &field,
							 const std::string &operation)
{
	if (!is_lower_hex(value, 32U) || !is_nonzero_hex(value)) {
		return Result<std::array<std::uint8_t, 16>>::failure(make_error(
			ErrorCode::FieldValueInvalid,
			"Protocol identity must be 32 lowercase hexadecimal characters and nonzero",
			{{"field", field}, {"operation", operation}}));
	}
	auto nibble = [](const char character) -> std::uint8_t {
		return character <= '9' ? static_cast<std::uint8_t>(character - '0')
					: static_cast<std::uint8_t>(character - 'a' + 10);
	};
	std::array<std::uint8_t, 16> decoded{};
	for (std::size_t index = 0; index < decoded.size(); ++index) {
		decoded[index] = static_cast<std::uint8_t>(
			(nibble(value[index * 2U]) << 4U) | nibble(value[index * 2U + 1U]));
	}
	return Result<std::array<std::uint8_t, 16>>::success(decoded);
}

Result<std::string> validate_release_id(std::string value, const std::string &operation)
{
	if (!is_release_id(value)) {
		return Result<std::string>::failure(make_error(
			ErrorCode::FieldValueInvalid,
			"Appliance release identity violates its bounded ASCII grammar",
			{{"field", "appliance_release_id"},
			 {"maximum_bytes", "128"},
			 {"operation", operation}}));
	}
	return Result<std::string>::success(std::move(value));
}

Result<std::string> validate_snapshot_id(std::string value, const std::string &operation)
{
	if (value.size() != 71U || value.compare(0, 7, "sha256:") != 0 ||
	    !is_lower_hex(std::string_view(value).substr(7), 64U)) {
		return Result<std::string>::failure(make_error(
			ErrorCode::FieldValueInvalid,
			"Snapshot identity must be sha256 followed by lowercase hexadecimal",
			{{"field", "snapshot_id"}, {"operation", operation}}));
	}
	return Result<std::string>::success(std::move(value));
}

template <typename Enum>
Result<Enum> parse_enum(const std::string &field, const std::string &value,
			const std::map<std::string, Enum> &allowed,
			const std::string &operation)
{
	const auto iterator = allowed.find(value);
	if (iterator == allowed.end()) {
		return Result<Enum>::failure(make_error(
			ErrorCode::FieldValueInvalid, "Protocol field contains an unknown value",
			{{"field", field}, {"operation", operation}}));
	}
	return Result<Enum>::success(iterator->second);
}

Result<std::uint64_t> validate_safe_positive_integer(const std::uint64_t value,
						     const std::string &field,
						     const std::string &operation)
{
	if (value == 0U || value > kMaximumSafeJsonInteger) {
		return Result<std::uint64_t>::failure(make_error(
			ErrorCode::FieldValueInvalid,
			"Protocol sequence is outside the safe positive integer range",
			{{"field", field},
			 {"maximum", std::to_string(kMaximumSafeJsonInteger)},
			 {"minimum", "1"},
			 {"operation", operation}}));
	}
	return Result<std::uint64_t>::success(value);
}

Result<std::int64_t> validate_observed_milliseconds(const std::int64_t value,
						    const std::string &operation)
{
	if (value < kMinimumObservedAtUnixMilliseconds ||
	    value >= kMaximumObservedAtUnixMillisecondsExclusive) {
		return Result<std::int64_t>::failure(make_error(
			ErrorCode::FieldValueInvalid,
			"Observed time is outside the protocol's supported range",
			{{"field", "observed_at_unix_milliseconds"},
			 {"operation", operation}}));
	}
	return Result<std::int64_t>::success(value);
}

Result<std::uint8_t> validate_brightness(const std::uint64_t value,
					 const std::string &operation)
{
	if (value > 100U) {
		return Result<std::uint8_t>::failure(make_error(
			ErrorCode::FieldValueInvalid,
			"Effective brightness must be in the inclusive range 0 through 100",
			{{"field", "effective_brightness_percent"},
			 {"maximum", "100"},
			 {"minimum", "0"},
			 {"operation", operation}}));
	}
	return Result<std::uint8_t>::success(static_cast<std::uint8_t>(value));
}

template <typename Enum>
Result<Enum> validate_enum_value(const Enum value, const std::string &field,
				 const std::initializer_list<Enum> allowed,
				 const std::string &operation)
{
	if (std::find(allowed.begin(), allowed.end(), value) == allowed.end()) {
		return Result<Enum>::failure(make_error(
			ErrorCode::FieldValueInvalid,
			"Typed protocol field contains an unknown enum value",
			{{"field", field}, {"operation", operation}}));
	}
	return Result<Enum>::success(value);
}

void append_uint16_be(std::vector<std::uint8_t> &output, const std::uint16_t value)
{
	output.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
	output.push_back(static_cast<std::uint8_t>(value & 0xffU));
}

void append_uint64_be(std::vector<std::uint8_t> &output, const std::uint64_t value)
{
	for (int shift = 56; shift >= 0; shift -= 8)
		output.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffU));
}

std::string lowercase_hex(const unsigned char *bytes, const std::size_t size)
{
	constexpr char kHex[] = "0123456789abcdef";
	std::string encoded;
	encoded.resize(size * 2U);
	for (std::size_t index = 0; index < size; ++index) {
		encoded[index * 2U] = kHex[(bytes[index] >> 4U) & 0x0fU];
		encoded[index * 2U + 1U] = kHex[bytes[index] & 0x0fU];
	}
	return encoded;
}

Result<std::uint8_t> protocol_version(const JsonObject &object, const std::string &operation)
{
	const auto parsed = require_uint64(object, "protocol_version", operation);
	if (!parsed)
		return Result<std::uint8_t>::failure(parsed.error());
	if (parsed.value() != kProtocolVersion) {
		return Result<std::uint8_t>::failure(make_error(
			ErrorCode::VersionUnsupported,
			"Protocol frame uses an unsupported version",
			{{"field", "protocol_version"},
			 {"operation", operation},
			 {"supported_version", "2"}}));
	}
	return Result<std::uint8_t>::success(kProtocolVersion);
}

Result<std::string> message_type(const JsonObject &object, const std::string &expected,
				 const std::string &operation)
{
	auto parsed = require_string(object, "message_type", operation);
	if (!parsed)
		return parsed;
	if (parsed.value() != expected) {
		return Result<std::string>::failure(make_error(
			ErrorCode::FieldValueInvalid, "Protocol message type is not valid here",
			{{"field", "message_type"}, {"operation", operation}}));
	}
	return parsed;
}

const std::map<std::string, DisplayMode> kDisplayModes{
	{"day_active", DisplayMode::DayActive},
	{"diagnostic", DisplayMode::Diagnostic},
	{"night_red", DisplayMode::NightRed},
	{"suspended", DisplayMode::Suspended},
};

const std::map<std::string, PresenceState> kPresenceStates{
	{"absent", PresenceState::Absent},
	{"grace_period", PresenceState::GracePeriod},
	{"present", PresenceState::Present},
	{"unknown", PresenceState::Unknown},
};

const std::map<std::string, SolarState> kSolarStates{
	{"day", SolarState::Day}, {"night", SolarState::Night}, {"unknown", SolarState::Unknown}};

const std::map<std::string, SystemState> kSystemStates{
	{"active", SystemState::Active},
	{"faulted", SystemState::Faulted},
	{"recomputing_after_resume", SystemState::RecomputingAfterResume},
	{"starting", SystemState::Starting},
	{"suspended", SystemState::Suspended},
};

const std::map<std::string, PresentationResult> kPresentationResults{
	{"committed", PresentationResult::Committed}, {"failed", PresentationResult::Failed}};

const std::set<std::string> kSnapshotFields{
	"appliance_release_id",
	"connection_challenge",
	"display_mode",
	"effective_brightness_percent",
	"event_sequence",
	"message_type",
	"observed_at_unix_milliseconds",
	"presence_state",
	"protocol_version",
	"snapshot_id",
	"solar_state",
	"supervisor_session_id",
	"system_state",
};

const std::set<std::string> kAcknowledgementFields{
	"appliance_release_id",
	"connection_challenge",
	"display_mode",
	"event_sequence",
	"message_type",
	"presentation_result",
	"protocol_version",
	"render_generation",
	"snapshot_id",
	"supervisor_session_id",
};

Result<std::string> bounded_encoded_frame(std::string frame, const std::string &operation)
{
	frame.push_back('\n');
	if (frame.size() > kMaximumFrameBytes) {
		return Result<std::string>::failure(make_error(
			ErrorCode::FrameTooLarge,
			"Encoded protocol frame exceeds the maximum byte count",
			{{"maximum_bytes", std::to_string(kMaximumFrameBytes)},
			 {"operation", operation}}));
	}
	return Result<std::string>::success(std::move(frame));
}

} // namespace

Result<std::int64_t> observed_at_unix_milliseconds(
	const std::chrono::system_clock::time_point observed_at)
{
	const auto milliseconds =
		std::chrono::floor<std::chrono::milliseconds>(observed_at.time_since_epoch())
			.count();
	return validate_observed_milliseconds(milliseconds,
					      "observed_at_unix_milliseconds");
}

Result<std::vector<std::uint8_t>> snapshot_digest_preimage(const Snapshot &snapshot)
{
	constexpr std::string_view operation = "snapshot_digest_preimage";
	const auto release_result =
		validate_release_id(snapshot.appliance_release_id, std::string(operation));
	if (!release_result)
		return Result<std::vector<std::uint8_t>>::failure(release_result.error());
	const auto session_result = decode_identity_hex(
		snapshot.supervisor_session_id, "supervisor_session_id", std::string(operation));
	if (!session_result)
		return Result<std::vector<std::uint8_t>>::failure(session_result.error());
	const auto challenge_result = decode_identity_hex(
		snapshot.connection_challenge, "connection_challenge", std::string(operation));
	if (!challenge_result)
		return Result<std::vector<std::uint8_t>>::failure(challenge_result.error());
	const auto sequence_result = validate_safe_positive_integer(
		snapshot.event_sequence, "event_sequence", std::string(operation));
	if (!sequence_result)
		return Result<std::vector<std::uint8_t>>::failure(sequence_result.error());
	const auto brightness_result =
		validate_brightness(snapshot.effective_brightness_percent, std::string(operation));
	if (!brightness_result)
		return Result<std::vector<std::uint8_t>>::failure(brightness_result.error());
	const auto observed_result = validate_observed_milliseconds(
		snapshot.observed_at_unix_milliseconds, std::string(operation));
	if (!observed_result)
		return Result<std::vector<std::uint8_t>>::failure(observed_result.error());
	const auto display_result = validate_enum_value(
		snapshot.display_mode, "display_mode",
		{DisplayMode::DayActive, DisplayMode::NightRed, DisplayMode::Suspended,
		 DisplayMode::Diagnostic},
		std::string(operation));
	if (!display_result)
		return Result<std::vector<std::uint8_t>>::failure(display_result.error());
	const auto presence_result = validate_enum_value(
		snapshot.presence_state, "presence_state",
		{PresenceState::Unknown, PresenceState::Present, PresenceState::GracePeriod,
		 PresenceState::Absent},
		std::string(operation));
	if (!presence_result)
		return Result<std::vector<std::uint8_t>>::failure(presence_result.error());
	const auto solar_result = validate_enum_value(
		snapshot.solar_state, "solar_state",
		{SolarState::Unknown, SolarState::Day, SolarState::Night},
		std::string(operation));
	if (!solar_result)
		return Result<std::vector<std::uint8_t>>::failure(solar_result.error());
	const auto system_result = validate_enum_value(
		snapshot.system_state, "system_state",
		{SystemState::Starting, SystemState::Active, SystemState::Suspended,
		 SystemState::RecomputingAfterResume, SystemState::Faulted},
		std::string(operation));
	if (!system_result)
		return Result<std::vector<std::uint8_t>>::failure(system_result.error());

	std::vector<std::uint8_t> preimage;
	preimage.reserve(kDigestDomain.size() + 1U + 1U + 2U +
			 snapshot.appliance_release_id.size() + 16U + 16U + 8U + 5U + 8U);
	preimage.insert(preimage.end(), kDigestDomain.begin(), kDigestDomain.end());
	preimage.push_back(0U);
	preimage.push_back(kProtocolVersion);
	append_uint16_be(preimage,
			 static_cast<std::uint16_t>(snapshot.appliance_release_id.size()));
	preimage.insert(preimage.end(), snapshot.appliance_release_id.begin(),
			snapshot.appliance_release_id.end());
	preimage.insert(preimage.end(), session_result.value().begin(),
			session_result.value().end());
	preimage.insert(preimage.end(), challenge_result.value().begin(),
			challenge_result.value().end());
	append_uint64_be(preimage, snapshot.event_sequence);
	preimage.push_back(static_cast<std::uint8_t>(snapshot.display_mode));
	preimage.push_back(static_cast<std::uint8_t>(snapshot.presence_state));
	preimage.push_back(static_cast<std::uint8_t>(snapshot.solar_state));
	preimage.push_back(static_cast<std::uint8_t>(snapshot.system_state));
	preimage.push_back(snapshot.effective_brightness_percent);
	append_uint64_be(preimage,
			 static_cast<std::uint64_t>(snapshot.observed_at_unix_milliseconds));
	return Result<std::vector<std::uint8_t>>::success(std::move(preimage));
}

Result<std::string> compute_snapshot_id(const Snapshot &snapshot)
{
	const auto preimage_result = snapshot_digest_preimage(snapshot);
	if (!preimage_result)
		return Result<std::string>::failure(preimage_result.error());
	std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
	unsigned int digest_size = 0;
	const auto &preimage = preimage_result.value();
	if (EVP_Digest(preimage.data(), preimage.size(), digest.data(), &digest_size,
		       EVP_sha256(), nullptr) != 1 || digest_size != 32U) {
		return Result<std::string>::failure(make_error(
			ErrorCode::DigestComputeFailed,
			"OpenSSL could not compute the protocol snapshot digest",
			{{"operation", "compute_snapshot_id"}}));
	}
	return Result<std::string>::success("sha256:" + lowercase_hex(digest.data(), 32U));
}

Result<std::string> encode_snapshot(const Snapshot &snapshot)
{
	const auto digest_result = compute_snapshot_id(snapshot);
	if (!digest_result)
		return Result<std::string>::failure(digest_result.error());
	if (!snapshot.snapshot_id.empty() && snapshot.snapshot_id != digest_result.value()) {
		return Result<std::string>::failure(make_error(
			ErrorCode::SnapshotIdMismatch,
			"Caller-supplied snapshot identity does not match typed snapshot fields",
			{{"operation", "encode_snapshot"}}));
	}
	std::ostringstream json;
	json.imbue(std::locale::classic());
	json << "{\"protocol_version\":2,\"message_type\":\"" << kSnapshotMessageType
	     << "\",\"appliance_release_id\":\"" << snapshot.appliance_release_id
	     << "\",\"supervisor_session_id\":\"" << snapshot.supervisor_session_id
	     << "\",\"connection_challenge\":\"" << snapshot.connection_challenge
	     << "\",\"event_sequence\":" << snapshot.event_sequence
	     << ",\"snapshot_id\":\"" << digest_result.value()
	     << "\",\"display_mode\":\"" << to_string(snapshot.display_mode)
	     << "\",\"presence_state\":\"" << to_string(snapshot.presence_state)
	     << "\",\"solar_state\":\"" << to_string(snapshot.solar_state)
	     << "\",\"system_state\":\"" << to_string(snapshot.system_state)
	     << "\",\"effective_brightness_percent\":"
	     << static_cast<unsigned int>(snapshot.effective_brightness_percent)
	     << ",\"observed_at_unix_milliseconds\":"
	     << snapshot.observed_at_unix_milliseconds << '}';
	return bounded_encoded_frame(json.str(), "encode_snapshot");
}

Result<Snapshot> decode_snapshot(const std::string_view frame)
{
	constexpr std::string_view operation = "decode_snapshot";
	const auto object_result = parse_frame(frame, kSnapshotFields, std::string(operation));
	if (!object_result)
		return Result<Snapshot>::failure(object_result.error());
	const auto &object = object_result.value();
	const auto version_result = protocol_version(object, std::string(operation));
	if (!version_result)
		return Result<Snapshot>::failure(version_result.error());
	const auto type_result =
		message_type(object, std::string(kSnapshotMessageType), std::string(operation));
	if (!type_result)
		return Result<Snapshot>::failure(type_result.error());

	auto release_result = require_string(object, "appliance_release_id", std::string(operation));
	if (!release_result)
		return Result<Snapshot>::failure(release_result.error());
	release_result = validate_release_id(std::move(release_result).value(),
					     std::string(operation));
	if (!release_result)
		return Result<Snapshot>::failure(release_result.error());
	auto session_result =
		require_string(object, "supervisor_session_id", std::string(operation));
	if (!session_result)
		return Result<Snapshot>::failure(session_result.error());
	const auto session_bytes = decode_identity_hex(session_result.value(),
		"supervisor_session_id", std::string(operation));
	if (!session_bytes)
		return Result<Snapshot>::failure(session_bytes.error());
	auto challenge_result =
		require_string(object, "connection_challenge", std::string(operation));
	if (!challenge_result)
		return Result<Snapshot>::failure(challenge_result.error());
	const auto challenge_bytes = decode_identity_hex(challenge_result.value(),
		"connection_challenge", std::string(operation));
	if (!challenge_bytes)
		return Result<Snapshot>::failure(challenge_bytes.error());
	auto sequence_result = require_uint64(object, "event_sequence", std::string(operation));
	if (!sequence_result)
		return Result<Snapshot>::failure(sequence_result.error());
	sequence_result = validate_safe_positive_integer(sequence_result.value(),
		"event_sequence", std::string(operation));
	if (!sequence_result)
		return Result<Snapshot>::failure(sequence_result.error());
	auto snapshot_id_result =
		require_string(object, "snapshot_id", std::string(operation));
	if (!snapshot_id_result)
		return Result<Snapshot>::failure(snapshot_id_result.error());
	snapshot_id_result =
		validate_snapshot_id(std::move(snapshot_id_result).value(), std::string(operation));
	if (!snapshot_id_result)
		return Result<Snapshot>::failure(snapshot_id_result.error());

	const auto display_text = require_string(object, "display_mode", std::string(operation));
	if (!display_text)
		return Result<Snapshot>::failure(display_text.error());
	const auto display_result = parse_enum("display_mode", display_text.value(), kDisplayModes,
					       std::string(operation));
	if (!display_result)
		return Result<Snapshot>::failure(display_result.error());
	const auto presence_text =
		require_string(object, "presence_state", std::string(operation));
	if (!presence_text)
		return Result<Snapshot>::failure(presence_text.error());
	const auto presence_result = parse_enum("presence_state", presence_text.value(),
						 kPresenceStates, std::string(operation));
	if (!presence_result)
		return Result<Snapshot>::failure(presence_result.error());
	const auto solar_text = require_string(object, "solar_state", std::string(operation));
	if (!solar_text)
		return Result<Snapshot>::failure(solar_text.error());
	const auto solar_result = parse_enum("solar_state", solar_text.value(), kSolarStates,
					     std::string(operation));
	if (!solar_result)
		return Result<Snapshot>::failure(solar_result.error());
	const auto system_text = require_string(object, "system_state", std::string(operation));
	if (!system_text)
		return Result<Snapshot>::failure(system_text.error());
	const auto system_result = parse_enum("system_state", system_text.value(), kSystemStates,
					      std::string(operation));
	if (!system_result)
		return Result<Snapshot>::failure(system_result.error());
	auto brightness_raw =
		require_uint64(object, "effective_brightness_percent", std::string(operation));
	if (!brightness_raw)
		return Result<Snapshot>::failure(brightness_raw.error());
	const auto brightness_result =
		validate_brightness(brightness_raw.value(), std::string(operation));
	if (!brightness_result)
		return Result<Snapshot>::failure(brightness_result.error());
	auto observed_result =
		require_int64(object, "observed_at_unix_milliseconds", std::string(operation));
	if (!observed_result)
		return Result<Snapshot>::failure(observed_result.error());
	observed_result =
		validate_observed_milliseconds(observed_result.value(), std::string(operation));
	if (!observed_result)
		return Result<Snapshot>::failure(observed_result.error());

	Snapshot decoded{release_result.value(),
			 session_result.value(),
			 challenge_result.value(),
			 sequence_result.value(),
			 snapshot_id_result.value(),
			 display_result.value(),
			 presence_result.value(),
			 solar_result.value(),
			 system_result.value(),
			 brightness_result.value(),
			 observed_result.value()};
	const auto digest_result = compute_snapshot_id(decoded);
	if (!digest_result)
		return Result<Snapshot>::failure(digest_result.error());
	if (digest_result.value() != decoded.snapshot_id) {
		return Result<Snapshot>::failure(make_error(
			ErrorCode::SnapshotIdMismatch,
			"Snapshot identity does not match the decoded typed fields",
			{{"operation", std::string(operation)}}));
	}
	return Result<Snapshot>::success(std::move(decoded));
}

Result<std::string>
encode_presentation_acknowledgement(const PresentationAcknowledgement &acknowledgement)
{
	constexpr std::string_view operation = "encode_presentation_acknowledgement";
	const auto release_result =
		validate_release_id(acknowledgement.appliance_release_id, std::string(operation));
	if (!release_result)
		return Result<std::string>::failure(release_result.error());
	const auto session_result = decode_identity_hex(acknowledgement.supervisor_session_id,
		"supervisor_session_id", std::string(operation));
	if (!session_result)
		return Result<std::string>::failure(session_result.error());
	const auto challenge_result = decode_identity_hex(acknowledgement.connection_challenge,
		"connection_challenge", std::string(operation));
	if (!challenge_result)
		return Result<std::string>::failure(challenge_result.error());
	const auto sequence_result = validate_safe_positive_integer(
		acknowledgement.event_sequence, "event_sequence", std::string(operation));
	if (!sequence_result)
		return Result<std::string>::failure(sequence_result.error());
	const auto generation_result = validate_safe_positive_integer(
		acknowledgement.render_generation, "render_generation", std::string(operation));
	if (!generation_result)
		return Result<std::string>::failure(generation_result.error());
	const auto snapshot_result =
		validate_snapshot_id(acknowledgement.snapshot_id, std::string(operation));
	if (!snapshot_result)
		return Result<std::string>::failure(snapshot_result.error());
	const auto display_result = validate_enum_value(
		acknowledgement.display_mode, "display_mode",
		{DisplayMode::DayActive, DisplayMode::NightRed, DisplayMode::Suspended,
		 DisplayMode::Diagnostic},
		std::string(operation));
	if (!display_result)
		return Result<std::string>::failure(display_result.error());
	const auto presentation_result = validate_enum_value(
		acknowledgement.presentation_result, "presentation_result",
		{PresentationResult::Committed, PresentationResult::Failed},
		std::string(operation));
	if (!presentation_result)
		return Result<std::string>::failure(presentation_result.error());

	std::ostringstream json;
	json.imbue(std::locale::classic());
	json << "{\"protocol_version\":2,\"message_type\":\""
	     << kAcknowledgementMessageType << "\",\"appliance_release_id\":\""
	     << acknowledgement.appliance_release_id
	     << "\",\"supervisor_session_id\":\""
	     << acknowledgement.supervisor_session_id
	     << "\",\"connection_challenge\":\""
	     << acknowledgement.connection_challenge << "\",\"event_sequence\":"
	     << acknowledgement.event_sequence << ",\"snapshot_id\":\""
	     << acknowledgement.snapshot_id << "\",\"display_mode\":\""
	     << to_string(acknowledgement.display_mode) << "\",\"render_generation\":"
	     << acknowledgement.render_generation << ",\"presentation_result\":\""
	     << to_string(acknowledgement.presentation_result) << "\"}";
	return bounded_encoded_frame(json.str(), std::string(operation));
}

Result<PresentationAcknowledgement>
decode_presentation_acknowledgement(const std::string_view frame)
{
	constexpr std::string_view operation = "decode_presentation_acknowledgement";
	const auto object_result =
		parse_frame(frame, kAcknowledgementFields, std::string(operation));
	if (!object_result)
		return Result<PresentationAcknowledgement>::failure(object_result.error());
	const auto &object = object_result.value();
	const auto version_result = protocol_version(object, std::string(operation));
	if (!version_result)
		return Result<PresentationAcknowledgement>::failure(version_result.error());
	const auto type_result = message_type(object, std::string(kAcknowledgementMessageType),
					      std::string(operation));
	if (!type_result)
		return Result<PresentationAcknowledgement>::failure(type_result.error());

	auto release_result = require_string(object, "appliance_release_id", std::string(operation));
	if (!release_result)
		return Result<PresentationAcknowledgement>::failure(release_result.error());
	release_result =
		validate_release_id(std::move(release_result).value(), std::string(operation));
	if (!release_result)
		return Result<PresentationAcknowledgement>::failure(release_result.error());
	auto session_result =
		require_string(object, "supervisor_session_id", std::string(operation));
	if (!session_result)
		return Result<PresentationAcknowledgement>::failure(session_result.error());
	const auto session_bytes = decode_identity_hex(session_result.value(),
		"supervisor_session_id", std::string(operation));
	if (!session_bytes)
		return Result<PresentationAcknowledgement>::failure(session_bytes.error());
	auto challenge_result =
		require_string(object, "connection_challenge", std::string(operation));
	if (!challenge_result)
		return Result<PresentationAcknowledgement>::failure(challenge_result.error());
	const auto challenge_bytes = decode_identity_hex(challenge_result.value(),
		"connection_challenge", std::string(operation));
	if (!challenge_bytes)
		return Result<PresentationAcknowledgement>::failure(challenge_bytes.error());
	auto sequence_result = require_uint64(object, "event_sequence", std::string(operation));
	if (!sequence_result)
		return Result<PresentationAcknowledgement>::failure(sequence_result.error());
	sequence_result = validate_safe_positive_integer(sequence_result.value(),
		"event_sequence", std::string(operation));
	if (!sequence_result)
		return Result<PresentationAcknowledgement>::failure(sequence_result.error());
	auto snapshot_result = require_string(object, "snapshot_id", std::string(operation));
	if (!snapshot_result)
		return Result<PresentationAcknowledgement>::failure(snapshot_result.error());
	snapshot_result =
		validate_snapshot_id(std::move(snapshot_result).value(), std::string(operation));
	if (!snapshot_result)
		return Result<PresentationAcknowledgement>::failure(snapshot_result.error());
	const auto display_text = require_string(object, "display_mode", std::string(operation));
	if (!display_text)
		return Result<PresentationAcknowledgement>::failure(display_text.error());
	const auto display_result = parse_enum("display_mode", display_text.value(), kDisplayModes,
					       std::string(operation));
	if (!display_result)
		return Result<PresentationAcknowledgement>::failure(display_result.error());
	auto generation_result =
		require_uint64(object, "render_generation", std::string(operation));
	if (!generation_result)
		return Result<PresentationAcknowledgement>::failure(generation_result.error());
	generation_result = validate_safe_positive_integer(generation_result.value(),
		"render_generation", std::string(operation));
	if (!generation_result)
		return Result<PresentationAcknowledgement>::failure(generation_result.error());
	const auto presentation_text =
		require_string(object, "presentation_result", std::string(operation));
	if (!presentation_text)
		return Result<PresentationAcknowledgement>::failure(presentation_text.error());
	const auto presentation_result =
		parse_enum("presentation_result", presentation_text.value(),
			   kPresentationResults, std::string(operation));
	if (!presentation_result)
		return Result<PresentationAcknowledgement>::failure(presentation_result.error());

	return Result<PresentationAcknowledgement>::success(PresentationAcknowledgement{
		release_result.value(), session_result.value(), challenge_result.value(),
		sequence_result.value(), snapshot_result.value(), display_result.value(),
		generation_result.value(), presentation_result.value()});
}

} // namespace bhp::panel_mode_protocol
