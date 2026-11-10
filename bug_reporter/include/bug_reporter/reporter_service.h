#pragma once
#include "bhp/durable_store/durable_store.h"
#include "bug_reporter/inbox_client.h"
#include "bug_reporter/ingest.h"
#include "bug_reporter/log_rings.h"
#include "bug_reporter/report_outbox.h"
#include "bug_reporter/reporter_protocol.h"
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

/**
 * @canonical bug_reporter_service -- what `bhp-reporter` does (bug-reports-and-self-repair.md, step 1).
 *
 * It keeps every service's log lines in RAM rings and faults in the persistent
 * fault ring. It turns faults and unexpected exits into automatic reports, and
 * the UI's snapshots and submits into manual ones, within the outbox's rate
 * limits. In `tick()` it sends queued bundles to the PC's inbox, the
 * repeat counts, and reads back each report's status. It holds no audio
 * device and no credential: the inbox knows the panel by its tailnet node.
 */
namespace bug_reporter
{
using RandomBytes = std::function<bool(unsigned char *bytes, std::size_t size)>;

struct ReporterSettings {
	std::string outbox_directory;
	std::string fault_directory;
	std::string build_info_path{"/etc/beaglebone-house-panel/image-version"};
	InboxSettings inbox;
	OutboxLimits outbox;
	RateLimits rates;
	BundleLimits bundle;
	/** How much log history a report carries. */
	std::chrono::seconds log_window{600};
	std::chrono::minutes status_interval{15};
	/** A snapshot not submitted within this is forgotten. */
	std::chrono::minutes snapshot_lifetime{10};
	std::size_t maximum_snapshots{4};
	/**
	 * Where the broker hands off bug_report takes (spoken descriptions); empty
	 * refuses voice reports with audio (REPORT.AUDIO_UNAVAILABLE). Takes are
	 * deleted once used and, unused, after `snapshot_lifetime`.
	 */
	std::string handoff_directory;
	/** The array channel the description keeps (unmeasured: channel 0 until the channel map is). */
	unsigned description_channel{0};
	std::uint64_t maximum_description_frames{60 * 16000};
	bhp::durable_store::BackoffPolicy backoff{std::chrono::seconds(5), std::chrono::hours(1), 0.2};
};

struct ReporterPorts {
	HttpTransport transport;
	std::function<std::chrono::steady_clock::time_point()> steady_now;
	/** Unix seconds, for dates and the limits that must survive a reboot. */
	std::function<std::int64_t()> unix_now;
	std::function<double()> random01;
	RandomBytes random_bytes;
};

class ReporterService final
{
public:
	/** REPORT.SETTINGS_INVALID, or the outbox's and fault ring's storage errors. */
	static Result<std::unique_ptr<ReporterService>> open(ReporterSettings settings, ReporterPorts ports);
	~ReporterService();

	/** One line from a service's stderr; a fault record files an automatic report. */
	void ingest(const std::string &service, const std::string &line);
	/** A service ended without being asked to: a crash (signal) or a restart (status). */
	void exited(const ExitLine &exit);
	/** Answers one UI request with the events to send back, in order. */
	std::vector<protocol::ReporterEvent> handle(const protocol::UiRequest &request);
	/** One round of uploads, counts and status reads; how long until it wants another. */
	std::chrono::milliseconds tick();
	/** The list the UI shows. */
	protocol::Reports reports() const;
	/** Rises whenever the list changed, so the daemon knows to send it. */
	std::uint64_t reports_sequence() const { return reports_sequence_; }
	/** The last automatic-report or upload failure (a REPORT.* code), for the daemon's log. */
	const std::string &last_error() const { return last_error_; }

private:
	struct Snapshot {
		ScreenState screen;
		std::chrono::steady_clock::time_point at;
		std::vector<std::string> lines;
	};
	ReporterService(ReporterSettings settings, ReporterPorts ports, std::unique_ptr<ReportOutbox> outbox,
			FaultRing faults, bhp::durable_store::Backoff backoff);
	std::optional<std::string> random_hex(std::size_t bytes);
	/** Sends one queued bundle's files and manifest. */
	Result<bool> send_bundle(const std::string &report_id);
	/** Sends the repeat counts; stops at the first retryable failure. */
	Result<bool> send_counts();
	std::vector<std::string> window_lines(bool with_faults) const;
	Result<std::string> file_report(ReportDraft draft);
	void automatic(const std::string &kind, const std::string &component, const std::string &code,
		       const std::string &location);
	Result<bool> upload_next();
	void poll_statuses();
	void purge_handoff();
	std::string created_at(std::int64_t unix_seconds) const;

	ReporterSettings settings_;
	ReporterPorts ports_;
	std::unique_ptr<ReportOutbox> outbox_;
	FaultRing faults_;
	bhp::durable_store::Backoff backoff_;
	InboxClient inbox_;
	LogRings rings_;
	std::map<std::string, Snapshot> snapshots_;
	std::chrono::steady_clock::time_point upload_due_{};
	std::chrono::steady_clock::time_point status_due_{};
	std::uint64_t reports_sequence_{1};
	std::string last_error_;
};
} // namespace bug_reporter
