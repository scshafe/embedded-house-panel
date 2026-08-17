#pragma once

#include "bhp/panel_mode_protocol/result.h"
#include "bhp/panel_mode_protocol/types.h"

#include <chrono>
#include <string>
#include <string_view>
#include <vector>

namespace bhp::panel_mode_protocol {

// Floors toward negative infinity before enforcing the version-2 wall-clock range.
[[nodiscard]] Result<std::int64_t> observed_at_unix_milliseconds(
	std::chrono::system_clock::time_point observed_at);

// @canonical panel_mode_protocol_codec -- the only v2 wire/digest implementation.
[[nodiscard]] Result<std::vector<std::uint8_t>>
snapshot_digest_preimage(const Snapshot &snapshot);

[[nodiscard]] Result<std::string> compute_snapshot_id(const Snapshot &snapshot);

[[nodiscard]] Result<std::string> encode_snapshot(const Snapshot &snapshot);

[[nodiscard]] Result<Snapshot> decode_snapshot(std::string_view frame);

[[nodiscard]] Result<std::string>
encode_presentation_acknowledgement(const PresentationAcknowledgement &acknowledgement);

[[nodiscard]] Result<PresentationAcknowledgement>
decode_presentation_acknowledgement(std::string_view frame);

} // namespace bhp::panel_mode_protocol
