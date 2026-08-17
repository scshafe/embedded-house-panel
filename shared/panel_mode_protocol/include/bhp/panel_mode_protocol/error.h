#pragma once

#include <map>
#include <string>
#include <string_view>

namespace bhp::panel_mode_protocol {

enum class ErrorCode {
	FrameTooLarge,
	FrameEncodingInvalid,
	JsonInvalid,
	FieldMissing,
	FieldDuplicate,
	FieldUnknown,
	FieldTypeInvalid,
	FieldValueInvalid,
	VersionUnsupported,
	DigestComputeFailed,
	SnapshotIdMismatch,
};

[[nodiscard]] constexpr std::string_view to_string(const ErrorCode code) noexcept
{
	switch (code) {
	case ErrorCode::FrameTooLarge:
		return "PANEL_MODE_PROTOCOL.FRAME_TOO_LARGE";
	case ErrorCode::FrameEncodingInvalid:
		return "PANEL_MODE_PROTOCOL.FRAME_ENCODING_INVALID";
	case ErrorCode::JsonInvalid:
		return "PANEL_MODE_PROTOCOL.JSON_INVALID";
	case ErrorCode::FieldMissing:
		return "PANEL_MODE_PROTOCOL.FIELD_MISSING";
	case ErrorCode::FieldDuplicate:
		return "PANEL_MODE_PROTOCOL.FIELD_DUPLICATE";
	case ErrorCode::FieldUnknown:
		return "PANEL_MODE_PROTOCOL.FIELD_UNKNOWN";
	case ErrorCode::FieldTypeInvalid:
		return "PANEL_MODE_PROTOCOL.FIELD_TYPE_INVALID";
	case ErrorCode::FieldValueInvalid:
		return "PANEL_MODE_PROTOCOL.FIELD_VALUE_INVALID";
	case ErrorCode::VersionUnsupported:
		return "PANEL_MODE_PROTOCOL.VERSION_UNSUPPORTED";
	case ErrorCode::DigestComputeFailed:
		return "PANEL_MODE_PROTOCOL.DIGEST_COMPUTE_FAILED";
	case ErrorCode::SnapshotIdMismatch:
		return "PANEL_MODE_PROTOCOL.SNAPSHOT_ID_MISMATCH";
	}
	return "PANEL_MODE_PROTOCOL.UNKNOWN";
}

struct Error {
	ErrorCode code;
	std::string message;
	std::map<std::string, std::string> context;
};

} // namespace bhp::panel_mode_protocol
