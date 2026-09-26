#include "bhp/touch_observation_protocol/touch_observation_codec.h"
#include "bhp/touch_observation_protocol/touch_observation_validation.h"
#include <charconv>

namespace bhp::touch_observation_protocol {
namespace {
// Fixed field order makes the small observation grammar unambiguous. This is
// deliberately not a permissive generic JSON parser at a privileged boundary.
struct Reader {
	std::string_view remaining;
	bool literal(std::string_view text) {
		if (remaining.substr(0, text.size()) != text) return false;
		remaining.remove_prefix(text.size()); return true;
	}
	bool string(std::string &out) {
		if (!literal("\"")) return false;
		auto end = remaining.find('"');
		if (end == std::string_view::npos) return false;
		out = std::string(remaining.substr(0, end));
		remaining.remove_prefix(end + 1); return true;
	}
	template<class T> bool number(T &out) {
		auto result = std::from_chars(remaining.data(), remaining.data() + remaining.size(), out);
		if (result.ec != std::errc{}) return false;
		remaining.remove_prefix(static_cast<std::size_t>(result.ptr - remaining.data()));
		return true;
	}
};
Error wire_error() { return {ErrorCode::WireInvalid, "Invalid normalized touch frame", {{"operation", "decode_touch_observation"}}}; }
}
Result<std::string> encode_touch_observation(const NormalizedTouchObservation &v) {
	auto valid = validate_normalized_touch_observation(v);
	if (!valid) return Result<std::string>::failure(valid.error());
	const auto &b = v.presentation_binding;
	std::string out = "{\"touch_observation_protocol_version\":" + std::to_string(v.touch_observation_protocol_version)
		+ ",\"touch_observation_id\":\"" + v.touch_observation_id
		+ "\",\"observation_sequence\":" + std::to_string(v.observation_sequence)
		+ ",\"lifecycle_protocol_version\":" + std::to_string(b.lifecycle_protocol_version)
		+ ",\"supervisor_session_id\":\"" + b.supervisor_session_id
		+ "\",\"lifecycle_event_sequence\":" + std::to_string(b.lifecycle_event_sequence)
		+ ",\"lifecycle_snapshot_id\":\"" + b.lifecycle_snapshot_id
		+ "\",\"ui_connection_challenge\":\"" + b.ui_connection_challenge
		+ "\",\"committed_render_generation\":" + std::to_string(b.committed_render_generation)
		+ ",\"raw_axis_x\":" + std::to_string(v.raw_axis_x)
		+ ",\"raw_axis_y\":" + std::to_string(v.raw_axis_y)
		+ ",\"contact_state\":\"" + std::string(to_string(v.contact_state))
		+ "\",\"supervisor_measured_contact_duration_milliseconds\":"
		+ std::to_string(v.supervisor_measured_contact_duration_milliseconds) + "}\n";
	return Result<std::string>::success(std::move(out));
}
Result<NormalizedTouchObservation> decode_touch_observation(std::string_view frame) {
	if (frame.size() > kMaximumTouchFrameBytes) return Result<NormalizedTouchObservation>::failure(wire_error());
	NormalizedTouchObservation v{}; auto &b = v.presentation_binding;
	Reader r{frame}; std::string state;
	if (!(r.literal("{\"touch_observation_protocol_version\":") && r.number(v.touch_observation_protocol_version)
		&& r.literal(",\"touch_observation_id\":") && r.string(v.touch_observation_id)
		&& r.literal(",\"observation_sequence\":") && r.number(v.observation_sequence)
		&& r.literal(",\"lifecycle_protocol_version\":") && r.number(b.lifecycle_protocol_version)
		&& r.literal(",\"supervisor_session_id\":") && r.string(b.supervisor_session_id)
		&& r.literal(",\"lifecycle_event_sequence\":") && r.number(b.lifecycle_event_sequence)
		&& r.literal(",\"lifecycle_snapshot_id\":") && r.string(b.lifecycle_snapshot_id)
		&& r.literal(",\"ui_connection_challenge\":") && r.string(b.ui_connection_challenge)
		&& r.literal(",\"committed_render_generation\":") && r.number(b.committed_render_generation)
		&& r.literal(",\"raw_axis_x\":") && r.number(v.raw_axis_x)
		&& r.literal(",\"raw_axis_y\":") && r.number(v.raw_axis_y)
		&& r.literal(",\"contact_state\":") && r.string(state)
		&& r.literal(",\"supervisor_measured_contact_duration_milliseconds\":")
		&& r.number(v.supervisor_measured_contact_duration_milliseconds) && r.literal("}\n") && r.remaining.empty()))
		return Result<NormalizedTouchObservation>::failure(wire_error());
	if (state == "contact_started") v.contact_state = ContactState::Started;
	else if (state == "contact_updated") v.contact_state = ContactState::Updated;
	else if (state == "contact_completed") v.contact_state = ContactState::Completed;
	else return Result<NormalizedTouchObservation>::failure(wire_error());
	auto canonical = encode_touch_observation(v);
	if (!canonical) return Result<NormalizedTouchObservation>::failure(canonical.error());
	if (canonical.value() != frame) return Result<NormalizedTouchObservation>::failure(wire_error());
	return Result<NormalizedTouchObservation>::success(std::move(v));
}
}
