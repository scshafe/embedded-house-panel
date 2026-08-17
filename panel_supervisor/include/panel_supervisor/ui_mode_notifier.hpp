#pragma once

#include "panel_supervisor/presentation_outcome.hpp"
#include "panel_supervisor/result.hpp"
#include "panel_supervisor/ui_mode_notification.hpp"

#include <chrono>

namespace house_panel::supervisor {

// @extension-point ui_mode_notification -- privileged presentation TCB port. Production builds
// wire only the canonical exact-UID Unix adapter; host fakes exercise policy deterministically.
class UiModeNotifier {
public:
	virtual ~UiModeNotifier() = default;

	/**
	 * Publishes one complete, idempotent UI snapshot or returns a typed failure.
	 *
	 * The notification contains every value required by the versioned process
	 * protocol. Implementations must not infer missing policy state. A successful
	 * publish means only that a presentation is pending; it never proves that the
	 * requested pixels were committed.
	 */
	[[nodiscard]] virtual Result<PendingPresentation>
	publish(const UiModeNotification &notification) = 0;

	/**
	 * Consumes the matching pending presentation no later than `deadline`.
	 *
	 * A committed outcome is eligible only when `publish` already returned a
	 * complete immutable commitment expectation. Late connections may be retained
	 * for the next publication but cannot amend or satisfy this one. The final
	 * power owner compares every binding field and generation constraint.
	 *
	 * Committed, not-committed, and failed presentations are explicit values, not
	 * cycle errors. Implementations must identify timeout, disconnect, stale,
	 * duplicate, mismatch, and explicit negative acknowledgement honestly.
	 */
	[[nodiscard]] virtual PresentationOutcome await_presentation(
		const PendingPresentation &pending_presentation,
		std::chrono::steady_clock::time_point deadline) = 0;
};

} // namespace house_panel::supervisor
