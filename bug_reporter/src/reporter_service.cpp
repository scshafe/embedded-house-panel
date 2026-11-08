#include "bug_reporter/reporter_service.h"
#include <algorithm>
#include <ctime>
#include <fstream>
#include <iterator>
#include <set>

namespace bug_reporter
{
namespace
{
namespace wire = bug_reporter::protocol;

bool terminal(const std::string &status)
{
	return status == "installed" || status == "wont_fix";
}

std::string date_of(std::int64_t unix_seconds)
{
	const std::time_t time = static_cast<std::time_t>(unix_seconds);
	std::tm parts{};
	gmtime_r(&time, &parts);
	char text[16];
	std::strftime(text, sizeof text, "%Y-%m-%d", &parts);
	return text;
}
} // namespace

Result<std::unique_ptr<ReporterService>> ReporterService::open(ReporterSettings settings, ReporterPorts ports)
{
	using Output = Result<std::unique_ptr<ReporterService>>;
	if (settings.outbox_directory.empty() || settings.fault_directory.empty() || settings.inbox.base_url.empty() ||
	    !ports.transport || !ports.steady_now || !ports.unix_now || !ports.random01 || !ports.random_bytes)
		return Output::failure({"REPORT.SETTINGS_INVALID", "The reporter's directories, inbox or ports are missing", {}, false});
	auto outbox = ReportOutbox::open(settings.outbox_directory, settings.outbox, settings.rates);
	if (!outbox.has_value())
		return Output::failure(outbox.error());
	auto faults = FaultRing::open(settings.fault_directory);
	if (!faults.has_value())
		return Output::failure(faults.error());
	auto backoff = bhp::durable_store::Backoff::create(settings.backoff, ports.random01);
	if (!backoff.has_value())
		return Output::failure({backoff.error().code, backoff.error().message, {}, false});
	std::unique_ptr<ReporterService> service(new ReporterService(std::move(settings), std::move(ports),
								     std::move(outbox.value()), faults.value(), backoff.value()));
	const auto now = service->ports_.steady_now();
	service->upload_due_ = now;
	service->status_due_ = now;
	return Output::success(std::move(service));
}

ReporterService::ReporterService(ReporterSettings settings, ReporterPorts ports, std::unique_ptr<ReportOutbox> outbox,
				 FaultRing faults, bhp::durable_store::Backoff backoff)
	: settings_(std::move(settings)), ports_(std::move(ports)), outbox_(std::move(outbox)), faults_(std::move(faults)),
	  backoff_(std::move(backoff)), inbox_(settings_.inbox, ports_.transport)
{
}

ReporterService::~ReporterService() = default;

std::optional<std::string> ReporterService::random_hex(std::size_t bytes)
{
	std::vector<unsigned char> raw(bytes);
	if (!ports_.random_bytes(raw.data(), raw.size()))
		return std::nullopt;
	static const char *hex = "0123456789abcdef";
	std::string out;
	for (const auto byte : raw) {
		out += hex[byte >> 4];
		out += hex[byte & 15];
	}
	return out;
}

std::string ReporterService::created_at(std::int64_t unix_seconds) const
{
	const std::time_t time = static_cast<std::time_t>(unix_seconds);
	std::tm parts{};
	gmtime_r(&time, &parts);
	char text[32];
	std::strftime(text, sizeof text, "%Y-%m-%dT%H:%M:%SZ", &parts);
	return text;
}

std::vector<std::string> ReporterService::window_lines(bool with_faults) const
{
	std::vector<std::string> lines;
	std::set<std::string> in_ram;
	const auto ram = rings_.window(ports_.steady_now() - settings_.log_window);
	for (const auto &line : ram)
		in_ram.insert(line.line);
	// Faults from before a restart survive only in the persistent ring.
	if (with_faults) {
		auto persisted = faults_.read();
		if (persisted.has_value())
			for (const auto &line : persisted.value())
				if (!in_ram.count(line))
					lines.push_back(line);
	}
	for (const auto &line : ram)
		lines.push_back(line.line);
	return lines;
}

Result<std::string> ReporterService::file_report(ReportDraft draft)
{
	using Output = Result<std::string>;
	const auto id_hex = random_hex(16);
	if (!id_hex)
		return Output::failure({"REPORT.RANDOM_UNAVAILABLE", "No randomness for a report ID", {}, false});
	const auto report_id = "rpt-" + *id_hex;
	const auto now = ports_.unix_now();
	auto admitted = outbox_->admit(draft.kind, fingerprint(draft, report_id), now);
	if (!admitted.has_value())
		return Output::failure(admitted.error());
	if (admitted.value().admission == Admission::Refused)
		return Output::failure({admitted.value().refusal_code, "Too many reports", {}, false});
	if (admitted.value().admission == Admission::CountOnly)
		return Output::success(std::string());
	const auto directory = outbox_->bundle_directory(report_id);
	auto bundle = write_bundle(directory, report_id, created_at(now), draft, read_build_info(settings_.build_info_path),
				   settings_.bundle);
	if (!bundle.has_value()) {
		remove_bundle_directory(directory);
		return Output::failure(bundle.error());
	}
	auto added = outbox_->add(bundle.value(), draft.kind, now);
	if (!added.has_value())
		return Output::failure(added.error());
	++reports_sequence_;
	upload_due_ = std::min(upload_due_, ports_.steady_now());
	return Output::success(report_id);
}

void ReporterService::automatic(const std::string &kind, const std::string &component, const std::string &code,
				const std::string &location)
{
	ReportDraft draft;
	draft.kind = kind;
	draft.component = component;
	draft.error_code = code;
	draft.location = location;
	draft.log_lines = window_lines(true);
	auto filed = file_report(std::move(draft));
	if (!filed.has_value())
		last_error_ = filed.error().code;
}

void ReporterService::ingest(const std::string &service, const std::string &line)
{
	if (!valid_service(service))
		return;
	rings_.append(service, line, ports_.steady_now());
	const auto classified = classify(line);
	const auto *record = std::get_if<LogRecord>(&classified);
	if (!record || !record->fault_code)
		return;
	(void)faults_.append(line);
	automatic("fault", service, *record->fault_code, record->fault_location);
}

void ReporterService::exited(const ExitLine &exit)
{
	if (!valid_service(exit.service) || (exit.status == 0 && exit.signal == 0))
		return;
	const auto line = encode_exit(exit);
	rings_.append(exit.service, line, ports_.steady_now());
	(void)faults_.append(line);
	if (exit.signal != 0)
		automatic("crash", exit.service, "PROCESS.SIGNAL_" + std::to_string(exit.signal), "exit");
	else
		automatic("restart", exit.service, "PROCESS.EXIT_" + std::to_string(exit.status), "exit");
}

std::vector<wire::ReporterEvent> ReporterService::handle(const wire::UiRequest &request)
{
	const auto now = ports_.steady_now();
	for (auto it = snapshots_.begin(); it != snapshots_.end();)
		it = now - it->second.at > settings_.snapshot_lifetime ? snapshots_.erase(it) : std::next(it);
	if (const auto *snapshot = std::get_if<wire::SnapshotRequest>(&request)) {
		if (snapshots_.size() >= settings_.maximum_snapshots)
			snapshots_.erase(std::min_element(snapshots_.begin(), snapshots_.end(), [](const auto &a, const auto &b) {
				return a.second.at < b.second.at;
			}));
		const auto id = random_hex(8);
		if (!id)
			return {wire::Refused{"snapshot", "REPORT.RANDOM_UNAVAILABLE"}};
		// The context is the moment of the tap: the logs are copied now.
		snapshots_["snap-" + *id] = {snapshot->screen, now, window_lines(false)};
		return {wire::SnapshotTaken{"snap-" + *id}};
	}
	if (const auto *cancel = std::get_if<wire::CancelRequest>(&request)) {
		snapshots_.erase(cancel->snapshot_id);
		return {reports()};
	}
	if (const auto *submit = std::get_if<wire::SubmitRequest>(&request)) {
		const auto found = snapshots_.find(submit->snapshot_id);
		if (found == snapshots_.end())
			return {wire::Refused{"submit", "REPORT.SNAPSHOT_EXPIRED"}, reports()};
		ReportDraft draft;
		draft.kind = "text";
		draft.component = "panel_ui";
		draft.screen = found->second.screen;
		draft.description_text = submit->text;
		draft.log_lines = found->second.lines;
		snapshots_.erase(found);
		auto filed = file_report(std::move(draft));
		if (!filed.has_value())
			return {wire::Refused{"submit", filed.error().code}, reports()};
		return {wire::Submitted{filed.value()}, reports()};
	}
	return {reports()};
}

Result<bool> ReporterService::upload_next()
{
	const auto queued = outbox_->queued();
	if (queued.empty())
		return Result<bool>::success(false);
	const auto &entry = queued.front();
	const auto directory = outbox_->bundle_directory(entry.report_id);
	std::ifstream input(directory + "/report.json");
	const std::string manifest{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
	const auto parsed = nlohmann::json::parse(manifest, nullptr, false);
	if (!parsed.is_object() || !parsed.contains("files") || !parsed["files"].is_array())
		return Result<bool>::failure({"REPORT.STORAGE_INVALID", "A queued bundle has no manifest", {}, false});
	for (const auto &file : parsed["files"]) {
		const auto name = file.value("name", std::string());
		auto sent = inbox_.upload_file(entry.report_id, name, directory + "/" + name);
		if (!sent.has_value())
			return sent;
	}
	auto completed = inbox_.complete(entry.report_id, manifest);
	if (!completed.has_value())
		return completed;
	auto received = outbox_->mark_received(entry.report_id);
	if (received.has_value())
		++reports_sequence_;
	return received;
}

void ReporterService::poll_statuses()
{
	std::vector<std::string> ids;
	for (const auto &entry : outbox_->recent(wire::kMaximumListed))
		if (entry.state == "received" && !terminal(entry.status))
			ids.push_back(entry.report_id);
	if (ids.empty())
		return;
	auto statuses = inbox_.statuses(ids);
	if (!statuses.has_value()) {
		last_error_ = statuses.error().code;
		return;
	}
	for (const auto &status : statuses.value()) {
		if (std::find(ids.begin(), ids.end(), status.report_id) == ids.end())
			continue;
		auto changed = outbox_->set_status(status.report_id, status.status, status.pr);
		if (changed.has_value() && changed.value())
			++reports_sequence_;
	}
}

std::chrono::milliseconds ReporterService::tick()
{
	const auto now = ports_.steady_now();
	if (now >= upload_due_) {
		auto uploaded = upload_next();
		bool failed = !uploaded.has_value();
		for (const auto &[fingerprint, count] : outbox_->pending_counts()) {
			if (failed)
				break;
			auto sent = inbox_.send_count(fingerprint, count);
			if (!sent.has_value()) {
				uploaded = sent;
				failed = true;
			} else
				(void)outbox_->clear_count(fingerprint, count);
		}
		if (failed) {
			last_error_ = uploaded.error().code;
			upload_due_ = now + backoff_.next_delay();
		} else {
			backoff_.reset();
			// More queued bundles go at once; otherwise wait for the next report.
			upload_due_ = uploaded.value() ? now : now + std::chrono::minutes(1);
		}
	}
	if (now >= status_due_) {
		poll_statuses();
		status_due_ = now + settings_.status_interval;
	}
	const auto next = std::min(upload_due_, status_due_);
	return std::chrono::duration_cast<std::chrono::milliseconds>(std::max(next - now, std::chrono::steady_clock::duration::zero()));
}

wire::Reports ReporterService::reports() const
{
	wire::Reports list;
	for (const auto &entry : outbox_->recent(wire::kMaximumListed)) {
		list.items.push_back({entry.report_id, entry.kind, entry.status, entry.pr, date_of(entry.created_at)});
		list.update_ready = list.update_ready || entry.status == "update_ready";
	}
	return list;
}
} // namespace bug_reporter
