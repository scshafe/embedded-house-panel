#pragma once

#include "panel_supervisor/result.hpp"
#include "panel_supervisor/suspend.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace house_panel::supervisor {

/** Kernel-visible ALSA capture state observed at one instant. */
struct CaptureClosureObservation {
	/** Capture substreams (`cardN/pcmMc/subK`) whose status is not exactly `closed`. */
	std::vector<std::string> open_substreams;
	/** Processes holding a `/dev/snd/pcmC*D*c` capture descriptor, ascending. */
	std::vector<int> holder_pids;
	/**
	 * Processes whose descriptors the kernel refused to show (EACCES/EPERM). They cannot be
	 * named as holders or terminated, but any capture they hold keeps a substream open, so
	 * closure still fails; the substream status is the authoritative signal.
	 */
	std::size_t unreadable_processes{0};

	[[nodiscard]] bool closed() const noexcept
	{
		return open_substreams.empty() && holder_pids.empty();
	}
};

// @extension-point capture_closure_probe -- independent kernel-visible evidence of microphone
// capture closure. It never trusts the media broker's own telemetry.
class CaptureClosureProbe {
public:
	virtual ~CaptureClosureProbe() = default;

	/** Reads current evidence. Unreadable evidence is a failure, never "closed". */
	[[nodiscard]] virtual Result<CaptureClosureObservation> observe() = 0;
};

// @canonical procfs_capture_closure_probe -- reads
// `<proc>/asound/card*/pcm*c/sub*/status` and every `<proc>/<pid>/fd` link. A missing
// `<proc>/asound` means the kernel has no ALSA capture; a missing `<proc>` or an unreadable
// substream status is a failure. Status text other than exactly `closed` counts as open.
// Processes that exit during the scan are skipped; processes whose descriptors are hidden are
// counted, never assumed to hold nothing.
class ProcfsCaptureClosureProbe final : public CaptureClosureProbe {
public:
	explicit ProcfsCaptureClosureProbe(std::string proc_root = "/proc");

	[[nodiscard]] Result<CaptureClosureObservation> observe() override;

private:
	std::string proc_root_;
};

// @extension-point media_terminator -- the bounded broker-termination/media-off path.
class MediaTerminator {
public:
	virtual ~MediaTerminator() = default;

	/**
	 * Signals the capture holders this terminator owns: SIGTERM, or SIGKILL when `force`.
	 * Holders it does not own are left untouched, so closure stays unverified.
	 */
	[[nodiscard]] virtual Result<void> terminate(const std::vector<int> &holder_pids,
						      bool force) = 0;
};

// @canonical uid_scoped_media_terminator -- signals only holders whose real and effective UIDs
// are both the media broker's exact service UID (never root, never another role). A pidfd pins
// each process between the UID check and the signal, so a reused PID is never signalled.
class UidScopedMediaTerminator final : public MediaTerminator {
public:
	explicit UidScopedMediaTerminator(std::uint32_t broker_uid, std::string proc_root = "/proc");

	[[nodiscard]] Result<void> terminate(const std::vector<int> &holder_pids,
					      bool force) override;

private:
	std::uint32_t broker_uid_;
	std::string proc_root_;
};

/** Bounds for the termination path: positive, poll <= grace < deadline <= 30 s. */
struct CaptureClosureBounds {
	std::chrono::milliseconds termination_grace{500};
	std::chrono::milliseconds closure_deadline{2000};
	std::chrono::milliseconds poll_interval{50};
};

/** What the most recent suspend request observed, for tests and diagnostics. */
struct CaptureClosureReport {
	bool verified{false};
	bool termination_requested{false};
	bool forced{false};
	std::size_t initial_open_substreams{0};
	std::size_t initial_holders{0};
	std::size_t initial_unreadable_processes{0};
	std::chrono::milliseconds waited{0};
};

// @canonical capture_closure_gated_suspend -- the supervisor blanks first; this decorator then
// verifies independent capture closure immediately before the suspend-state write. Open capture
// takes the bounded termination path (SIGTERM, grace, SIGKILL, deadline). Unverified closure
// refuses the suspend with a typed error; it never guesses and never waits past the deadline.
class CaptureClosureGatedSuspend final : public Suspend {
public:
	using Sleeper = std::function<void(std::chrono::milliseconds)>;
	using Clock = std::function<std::chrono::steady_clock::time_point()>;

	/**
	 * `terminator` may be null: open capture then always refuses the suspend. Bounds must
	 * already satisfy validate_capture_closure_bounds. Empty `sleeper`/`clock` use the real
	 * steady clock.
	 */
	CaptureClosureGatedSuspend(Suspend &configured_suspend, CaptureClosureProbe &probe,
				   MediaTerminator *terminator, CaptureClosureBounds bounds,
				   Sleeper sleeper = {}, Clock clock = {});

	[[nodiscard]] Result<void> request_suspend() override;

	[[nodiscard]] const CaptureClosureReport &last_report() const noexcept
	{
		return last_report_;
	}

private:
	[[nodiscard]] Result<CaptureClosureObservation>
	wait_for_closure(std::chrono::steady_clock::time_point until);

	Suspend &configured_suspend_;
	CaptureClosureProbe &probe_;
	MediaTerminator *terminator_;
	CaptureClosureBounds bounds_;
	Sleeper sleeper_;
	Clock clock_;
	CaptureClosureReport last_report_;
};

/** Validates bounds before a daemon or test constructs the gate. */
[[nodiscard]] Result<CaptureClosureBounds> validate_capture_closure_bounds(
	CaptureClosureBounds bounds);

} // namespace house_panel::supervisor
