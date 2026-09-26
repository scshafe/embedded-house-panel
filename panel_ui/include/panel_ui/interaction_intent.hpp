#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace house_panel::panel_ui {

inline constexpr std::uint32_t kInteractionIntentProtocolVersion = 1;

enum class IntentSourceKind { Touch, Voice, HostFixture };

enum class IntentEffectClass {
	Navigation,
	StudyRating,
	RatingCorrection,
	CaptureStart,
	ExternalSubmission,
};

[[nodiscard]] std::string_view to_string(IntentSourceKind source_kind) noexcept;
[[nodiscard]] std::string_view to_string(IntentEffectClass effect_class) noexcept;

struct VoiceIntentEvidence {
	std::string spoken_form_id;
	std::string catalog_revision;
	std::string active_command_set_digest;
	std::string backend_artifact_digest;
	std::string recognizer_build_model_identity;
	std::string recognition_policy_digest;
};

// @canonical panel_interaction_intent -- the sole panel-wide semantic input envelope.
struct InteractionIntentEnvelope {
	std::uint32_t protocol_version;
	std::string active_context_nonce;
	std::string source_instance_id;
	IntentSourceKind source_kind;
	std::uint64_t source_sequence;
	std::uint64_t observed_monotonic_time_ns;
	std::uint64_t render_generation;
	std::string target_surface_id;
	std::string intent_id;
	std::optional<VoiceIntentEvidence> voice_evidence;
};

struct IntentDefinition {
	std::string intent_id;
	IntentEffectClass effect_class;
	bool enabled;
	bool allow_touch;
	bool allow_voice;
	bool allow_host_fixture;
	// Empty for local feature actions and launcher paging; never supplied by an input envelope.
	std::string navigation_surface_id{};
};

} // namespace house_panel::panel_ui
