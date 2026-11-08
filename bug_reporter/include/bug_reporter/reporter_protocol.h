#pragma once
#include "bug_reporter/report_bundle.h"
#include "bug_reporter/result.h"
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

/**
 * @canonical bug_reporter_protocol -- the UI-to-reporter exchange (bug-reports-and-self-repair.md).
 *
 * ADR 0005's framing: one compact JSON object per line with `version: 1` and
 * closed key sets, over a Unix socket whose peer is checked by exact UID both
 * ways, frames up to 16 KiB.
 *
 * The UI sends `snapshot` when the Report button is tapped (or "report a bug"
 * is heard), with the screen it was on, then `submit` with the typed text, or
 * `cancel`. `status` asks for the report list. The reporter answers
 * `snapshot_taken`, `submitted`, `reports` and `refused`. Report bodies never
 * cross back to the UI: the list carries codes, statuses and dates only.
 */
namespace bug_reporter::protocol
{
inline constexpr std::uint32_t kProtocolVersion = 1;
inline constexpr std::size_t kMaximumFrameBytes = 16 * 1024;
inline constexpr std::size_t kMaximumListed = 20;

struct SnapshotRequest {
	ScreenState screen;
};
struct SubmitRequest {
	std::string snapshot_id;
	/** Typed description, 500 characters at most; empty is allowed. */
	std::string text;
	/** A spoken description the broker handed off to the reporter (a bug_report take), or none. */
	std::optional<std::string> take_id;
};
struct CancelRequest {
	std::string snapshot_id;
};
struct StatusRequest {
};
using UiRequest = std::variant<SnapshotRequest, SubmitRequest, CancelRequest, StatusRequest>;

struct SnapshotTaken {
	std::string snapshot_id;
};
struct Submitted {
	std::string report_id;
};
struct ListedReport {
	std::string report_id;
	std::string kind;
	std::string status;
	std::optional<std::uint64_t> pr;
	/** `YYYY-MM-DD`, UTC. */
	std::string created_on;
};
struct Reports {
	std::vector<ListedReport> items;
	bool update_ready{false};
};
struct Refused {
	/** `snapshot`, `submit` or `cancel`. */
	std::string request;
	std::string error_code;
};
using ReporterEvent = std::variant<SnapshotTaken, Submitted, Reports, Refused>;

/** `snap-` and 16 hex digits. */
bool valid_snapshot_id(const std::string &snapshot_id);
Result<std::string> encode(const UiRequest &request);
Result<std::string> encode(const ReporterEvent &event);
/** REPORT.PROTOCOL_INVALID for anything but one whole, valid frame. */
Result<UiRequest> decode_ui_request(std::string_view frame);
Result<ReporterEvent> decode_reporter_event(std::string_view frame);

/**
 * @canonical bug_reporter_client -- the UI's side of one reporter connection.
 * The peer must be the exact reporter UID (REPORT.REPORTER_PEER_REJECTED);
 * a malformed event or a lost reporter closes it (REPORT.REPORTER_UNAVAILABLE).
 */
class ReporterClient final
{
public:
	static Result<std::unique_ptr<ReporterClient>> connect(const std::string &path, std::uint32_t reporter_uid);
	static Result<std::unique_ptr<ReporterClient>> for_connected_peer(int descriptor, std::uint32_t reporter_uid);
	~ReporterClient();
	ReporterClient(const ReporterClient &) = delete;
	ReporterClient &operator=(const ReporterClient &) = delete;
	Result<bool> send(const UiRequest &request);
	Result<std::vector<ReporterEvent>> poll();
	bool connected() const;

private:
	struct Channel;
	explicit ReporterClient(int descriptor);
	std::unique_ptr<Channel> channel_;
};
} // namespace bug_reporter::protocol
