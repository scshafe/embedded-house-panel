#include "bug_reporter/reporter_service.h"
#include "bug_reporter/description_audio.h"
#include <algorithm>
#include <filesystem>
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
		if (submit->take_id) {
			// A spoken description: one channel of the broker's bug_report take.
			draft.kind = "voice";
			const auto take = settings_.handoff_directory + "/" + *submit->take_id;
			if (settings_.handoff_directory.empty() || !valid_take_id(*submit->take_id))
				return {wire::Refused{"submit", "REPORT.AUDIO_UNAVAILABLE"}, reports()};
			std::ifstream record(take + "/capture.json");
			const auto capture = nlohmann::json::parse(record, nullptr, false);
			auto audio = capture.is_object() && capture.contains("purpose") && capture["purpose"] == "bug_report"
					     ? mono_description(take + "/original.wav", settings_.description_channel,
								settings_.maximum_description_frames)
					     : Result<std::string>::failure({"REPORT.AUDIO_INVALID", "Not a bug_report take", {}, false});
			// The take is used once, whatever happens next: no audio lingers here.
			std::error_code ignored;
			std::filesystem::remove_all(take, ignored);
			if (!audio.has_value())
				return {wire::Refused{"submit", audio.error().code}, reports()};
			draft.audio_wav = std::move(audio.value());
		}
		snapshots_.erase(found);
		auto filed = file_report(std::move(draft));
		if (!filed.has_value())
			return {wire::Refused{"submit", filed.error().code}, reports()};
		return {wire::Submitted{filed.value()}, reports()};
	}
	return {reports()};
}

Result<bool> ReporterService::send_bundle(const std::string &report_id)
{
	const auto directory = outbox_->bundle_directory(report_id);
	std::ifstream input(directory + "/report.json");
	const std::string manifest{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
	const auto parsed = nlohmann::json::parse(manifest, nullptr, false);
	if (!parsed.is_object() || !parsed.contains("files") || !parsed["files"].is_array())
		return Result<bool>::failure({"REPORT.STORAGE_INVALID", "A queued bundle has no manifest", {}, false});
	for (const auto &file : parsed["files"]) {
		if (!file.is_object() || !file.contains("name") || !file["name"].is_string())
			return Result<bool>::failure({"REPORT.STORAGE_INVALID", "A queued bundle's manifest is malformed", {}, false});
		const auto name = file["name"].get<std::string>();
		auto sent = inbox_.upload_file(report_id, name, directory + "/" + name);
		if (!sent.has_value())
			return sent;
	}
	return inbox_.complete(report_id, manifest);
}

Result<bool> ReporterService::upload_next()
{
	const auto queued = outbox_->queued();
	if (queued.empty())
		return Result<bool>::success(false);
	const auto report_id = queued.front().report_id;
	auto sent = send_bundle(report_id);
	if (!sent.has_value() && sent.error().retryable)
		return sent;
	// Sent, or refused for good (a 4xx about this bundle, or a bundle that cannot be read):
	// either way it leaves the queue, so one bad bundle never holds back the ones behind it.
	auto settled = sent.has_value() ? outbox_->mark_received(report_id) : outbox_->mark_refused(report_id);
	if (!settled.has_value())
		return settled;
	if (!sent.has_value())
		last_error_ = sent.error().code;
	++reports_sequence_;
	return Result<bool>::success(true);
}

Result<bool> ReporterService::send_counts()
{
	auto batches = outbox_->count_batches([this] { return random_hex(16); });
	if (!batches.has_value())
		return Result<bool>::failure(batches.error());
	for (const auto &batch : batches.value()) {
		auto sent = inbox_.send_count(batch.fingerprint, batch.batch, batch.count);
		if (!sent.has_value() && sent.error().retryable)
			return sent;
		// A refused update is dropped, so it never blocks the counts behind it.
		if (!sent.has_value())
			last_error_ = sent.error().code;
		auto cleared = outbox_->clear_count(batch.fingerprint, batch.batch);
		if (!cleared.has_value())
			return cleared;
	}
	return Result<bool>::success(true);
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

void ReporterService::purge_handoff()
{
	if (settings_.handoff_directory.empty())
		return;
	std::error_code error;
	const auto cutoff = std::filesystem::file_time_type::clock::now() - settings_.snapshot_lifetime;
	for (const auto &entry : std::filesystem::directory_iterator(settings_.handoff_directory, error)) {
		const auto name = entry.path().filename().string();
		if (name.rfind("take-", 0) != 0 && name.rfind(".incoming-", 0) != 0)
			continue;
		std::error_code stamp;
		const auto written = std::filesystem::last_write_time(entry.path(), stamp);
		if (!stamp && written < cutoff)
			std::filesystem::remove_all(entry.path(), stamp);
	}
}

std::chrono::milliseconds ReporterService::tick()
{
	const auto now = ports_.steady_now();
	purge_handoff();
	if (now >= upload_due_) {
		auto uploaded = upload_next();
		if (uploaded.has_value()) {
			auto counted = send_counts();
			if (!counted.has_value())
				uploaded = Result<bool>::failure(counted.error());
		}
		if (!uploaded.has_value()) {
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
