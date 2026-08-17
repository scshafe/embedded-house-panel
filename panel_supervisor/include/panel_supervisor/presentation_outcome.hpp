#pragma once

#include "panel_supervisor/supervisor_error.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace house_panel::supervisor {

// @canonical panel_presentation_outcome -- explicit result of publishing and awaiting one UI
// presentation without converting expected renderer or transport degradation into a cycle fault.
struct PresentationCommitmentExpectation {
	std::string appliance_release_id;
	std::string supervisor_session_id;
	std::string connection_challenge;
	std::string snapshot_id;
	std::string display_mode;
	std::uint64_t minimum_render_generation_exclusive;
};

struct PendingPresentation {
	std::uint64_t publication_sequence;
	std::optional<PresentationCommitmentExpectation> expected_commitment;
};

enum class PresentationNotCommittedReason {
	AcknowledgementUnavailable,
	TimedOut,
	Disconnected,
	Stale,
	Duplicate,
	Mismatched,
};

/** Returns the stable snake_case presentation non-commit reason used in diagnostics. */
[[nodiscard]] constexpr std::string_view
to_string(const PresentationNotCommittedReason reason) noexcept
{
	switch (reason) {
	case PresentationNotCommittedReason::AcknowledgementUnavailable:
		return "acknowledgement_unavailable";
	case PresentationNotCommittedReason::TimedOut:
		return "timed_out";
	case PresentationNotCommittedReason::Disconnected:
		return "disconnected";
	case PresentationNotCommittedReason::Stale:
		return "stale";
	case PresentationNotCommittedReason::Duplicate:
		return "duplicate";
	case PresentationNotCommittedReason::Mismatched:
		return "mismatched";
	}
	return "unrecognized_not_committed_reason";
}

enum class PresentationFailureReason {
	ExplicitFailure,
	PublicationFailed,
	AdapterContractViolation,
};

/** Returns the stable snake_case presentation failure reason used in diagnostics. */
[[nodiscard]] constexpr std::string_view
to_string(const PresentationFailureReason reason) noexcept
{
	switch (reason) {
	case PresentationFailureReason::ExplicitFailure:
		return "explicit_failure";
	case PresentationFailureReason::PublicationFailed:
		return "publication_failed";
	case PresentationFailureReason::AdapterContractViolation:
		return "adapter_contract_violation";
	}
	return "unrecognized_failure_reason";
}

struct PresentationCommitted {
	std::string appliance_release_id;
	std::uint64_t publication_sequence;
	std::uint64_t render_generation;
	std::string supervisor_session_id;
	std::string connection_challenge;
	std::string snapshot_id;
	std::string display_mode;
};

struct PresentationNotCommitted {
	PresentationNotCommittedReason reason;
};

struct PresentationFailed {
	PresentationFailureReason reason;
	SupervisorError error;
};

using PresentationOutcome =
	std::variant<PresentationCommitted, PresentationNotCommitted, PresentationFailed>;

} // namespace house_panel::supervisor
