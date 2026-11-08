#include "bug_reporter/report_outbox.h"
#include <algorithm>
#include <dirent.h>
#include <map>
#include <regex>
#include <unistd.h>

namespace bug_reporter
{
namespace
{
using Json = nlohmann::json;
constexpr std::int64_t kHour = 3600, kDay = 24 * 3600, kWeek = 7 * 24 * 3600;

struct FingerprintRecord {
	std::int64_t last_full_at{0};
	std::vector<std::int64_t> full_times;
	std::uint64_t pending_count{0};
};

void keep_since(std::vector<std::int64_t> &times, std::int64_t since)
{
	times.erase(std::remove_if(times.begin(), times.end(), [since](std::int64_t t) { return t < since; }), times.end());
}

std::size_t count_since(const std::vector<std::int64_t> &times, std::int64_t since)
{
	return static_cast<std::size_t>(std::count_if(times.begin(), times.end(), [since](std::int64_t t) { return t >= since; }));
}

bool valid_status(const std::string &status)
{
	static const std::regex pattern("queued|received|investigating|fix_in_pr|merged|update_ready|installed|wont_fix");
	return std::regex_match(status, pattern);
}
} // namespace

struct ReportOutbox::State {
	std::vector<OutboxEntry> reports;
	std::vector<std::int64_t> manual_times;
	std::vector<std::int64_t> automatic_times;
	std::map<std::string, FingerprintRecord> fingerprints;
};

void remove_bundle_directory(const std::string &directory)
{
	if (DIR *folder = ::opendir(directory.c_str())) {
		while (const dirent *entry = ::readdir(folder)) {
			const std::string name = entry->d_name;
			if (name != "." && name != "..")
				::unlink((directory + "/" + name).c_str());
		}
		::closedir(folder);
	}
	::rmdir(directory.c_str());
}

ReportOutbox::ReportOutbox(std::string directory, OutboxLimits limits, RateLimits rates)
	: directory_(directory), limits_(limits), rates_(rates),
	  file_(std::move(directory), {"outbox.lock", "outbox.json", "outbox.tmp", "REPORT"}, 1024 * 1024),
	  state_(std::make_unique<State>())
{
}

ReportOutbox::~ReportOutbox() = default;

Result<std::unique_ptr<ReportOutbox>> ReportOutbox::open(std::string directory, OutboxLimits limits, RateLimits rates)
{
	using Output = Result<std::unique_ptr<ReportOutbox>>;
	std::unique_ptr<ReportOutbox> outbox(new ReportOutbox(std::move(directory), limits, rates));
	auto read = outbox->file_.read();
	if (!read.has_value())
		return Output::failure({read.error().code, read.error().message, read.error().context, false});
	if (!read.value())
		return Output::success(std::move(outbox));
	const Json index = Json::parse(*read.value(), nullptr, false);
	if (!index.is_object() || index.value("schema_version", 0) != 1)
		return Output::failure({"REPORT.STORAGE_INVALID", "outbox.json is not a version 1 record", {}, false});
	auto &state = *outbox->state_;
	try {
		for (const auto &item : index.at("reports")) {
			OutboxEntry entry;
			entry.report_id = item.at("report_id").get<std::string>();
			entry.kind = item.at("kind").get<std::string>();
			entry.fingerprint = item.at("fingerprint").get<std::string>();
			entry.created_at = item.at("created_at").get<std::int64_t>();
			entry.bytes = item.at("bytes").get<std::uint64_t>();
			entry.state = item.at("state").get<std::string>();
			entry.status = item.at("status").get<std::string>();
			if (!item.at("pr").is_null())
				entry.pr = item.at("pr").get<std::uint64_t>();
			if (!valid_report_id(entry.report_id) || !valid_status(entry.status) ||
			    (entry.state != "queued" && entry.state != "received"))
				throw std::runtime_error("entry");
			state.reports.push_back(entry);
		}
		state.manual_times = index.at("manual_times").get<std::vector<std::int64_t>>();
		state.automatic_times = index.at("automatic_times").get<std::vector<std::int64_t>>();
		for (const auto &[fingerprint, item] : index.at("fingerprints").items())
			state.fingerprints[fingerprint] = {item.at("last_full_at").get<std::int64_t>(),
							   item.at("full_times").get<std::vector<std::int64_t>>(),
							   item.at("pending_count").get<std::uint64_t>()};
	} catch (const std::exception &) {
		return Output::failure({"REPORT.STORAGE_INVALID", "outbox.json does not hold valid entries", {}, false});
	}
	return Output::success(std::move(outbox));
}

Result<bool> ReportOutbox::commit()
{
	Json reports = Json::array();
	for (const auto &entry : state_->reports)
		reports.push_back({{"report_id", entry.report_id},
				   {"kind", entry.kind},
				   {"fingerprint", entry.fingerprint},
				   {"created_at", entry.created_at},
				   {"bytes", entry.bytes},
				   {"state", entry.state},
				   {"status", entry.status},
				   {"pr", entry.pr ? Json(*entry.pr) : Json(nullptr)}});
	Json fingerprints = Json::object();
	for (const auto &[fingerprint, record] : state_->fingerprints)
		fingerprints[fingerprint] = {{"last_full_at", record.last_full_at},
					     {"full_times", record.full_times},
					     {"pending_count", record.pending_count}};
	const Json index = {{"schema_version", 1},
			    {"reports", reports},
			    {"manual_times", state_->manual_times},
			    {"automatic_times", state_->automatic_times},
			    {"fingerprints", fingerprints}};
	auto committed = file_.commit(index.dump() + "\n");
	if (!committed.has_value())
		return Result<bool>::failure({committed.error().code, committed.error().message, committed.error().context, true});
	return Result<bool>::success(true);
}

Result<AdmissionResult> ReportOutbox::admit(const std::string &kind, const std::string &fingerprint, std::int64_t now)
{
	auto &state = *state_;
	keep_since(state.manual_times, now - kDay);
	keep_since(state.automatic_times, now - kDay);
	for (auto it = state.fingerprints.begin(); it != state.fingerprints.end();) {
		keep_since(it->second.full_times, now - kWeek);
		if (it->second.full_times.empty() && it->second.pending_count == 0 && it->second.last_full_at < now - kWeek)
			it = state.fingerprints.erase(it);
		else
			++it;
	}
	AdmissionResult result;
	if (manual_kind(kind)) {
		if (!state.manual_times.empty() && state.manual_times.back() > now - rates_.manual_spacing_seconds)
			result = {Admission::Refused, "REPORT.RATE_LIMITED"};
		else if (state.manual_times.size() >= rates_.manual_per_day)
			result = {Admission::Refused, "REPORT.DAILY_LIMIT"};
		else {
			result = {Admission::Full, ""};
			state.manual_times.push_back(now);
		}
	} else {
		auto &record = state.fingerprints[fingerprint];
		const bool recent = record.last_full_at > now - rates_.fingerprint_window_seconds;
		const bool weekly = record.full_times.size() >= rates_.full_per_fingerprint_per_week;
		const bool busy = count_since(state.automatic_times, now - kHour) >= rates_.automatic_per_hour ||
				  state.automatic_times.size() >= rates_.automatic_per_day;
		if (recent || weekly || busy) {
			result = {Admission::CountOnly, ""};
			++record.pending_count;
		} else {
			result = {Admission::Full, ""};
			record.last_full_at = now;
			record.full_times.push_back(now);
			state.automatic_times.push_back(now);
		}
	}
	auto committed = commit();
	if (!committed.has_value())
		return Result<AdmissionResult>::failure(committed.error());
	return Result<AdmissionResult>::success(result);
}

std::string ReportOutbox::bundle_directory(const std::string &report_id) const
{
	return directory_ + "/" + report_id;
}

Result<bool> ReportOutbox::add(const Bundle &bundle, const std::string &kind, std::int64_t created_at)
{
	auto &reports = state_->reports;
	const auto queued_load = [&reports] {
		std::uint64_t bytes = 0;
		std::size_t count = 0;
		for (const auto &entry : reports)
			if (entry.state == "queued") {
				bytes += entry.bytes;
				++count;
			}
		return std::make_pair(bytes, count);
	};
	for (;;) {
		const auto [bytes, count] = queued_load();
		if (bytes + bundle.bytes <= limits_.maximum_bytes && count + 1 <= limits_.maximum_reports)
			break;
		const auto victim = std::find_if(reports.begin(), reports.end(), [](const OutboxEntry &entry) {
			return entry.state == "queued" && !manual_kind(entry.kind);
		});
		if (victim == reports.end()) {
			remove_bundle_directory(bundle_directory(bundle.report_id));
			return Result<bool>::failure({"REPORT.OUTBOX_FULL", "The report queue is full", {}, false});
		}
		remove_bundle_directory(bundle_directory(victim->report_id));
		reports.erase(victim);
	}
	reports.push_back({bundle.report_id, kind, bundle.fingerprint, created_at, bundle.bytes, "queued", "queued", {}});
	// Keep only the newest received records beyond the status history.
	std::size_t received = 0;
	for (auto it = reports.rbegin(); it != reports.rend(); ++it)
		if (it->state == "received" && ++received > limits_.status_history)
			it->state = "drop";
	reports.erase(std::remove_if(reports.begin(), reports.end(), [](const OutboxEntry &e) { return e.state == "drop"; }),
		      reports.end());
	return commit();
}

std::vector<OutboxEntry> ReportOutbox::queued() const
{
	std::vector<OutboxEntry> entries;
	for (const auto &entry : state_->reports)
		if (entry.state == "queued")
			entries.push_back(entry);
	return entries;
}

Result<bool> ReportOutbox::mark_received(const std::string &report_id)
{
	for (auto &entry : state_->reports)
		if (entry.report_id == report_id && entry.state == "queued") {
			entry.state = "received";
			if (entry.status == "queued")
				entry.status = "received";
			auto committed = commit();
			if (committed.has_value())
				remove_bundle_directory(bundle_directory(report_id));
			return committed;
		}
	return Result<bool>::failure({"REPORT.UNKNOWN_REPORT", "No such queued report", {}, false});
}

Result<bool> ReportOutbox::set_status(const std::string &report_id, const std::string &status,
				      std::optional<std::uint64_t> pr)
{
	if (!valid_status(status))
		return Result<bool>::failure({"REPORT.STATUS_INVALID", "Unknown report status", {}, false});
	for (auto &entry : state_->reports)
		if (entry.report_id == report_id) {
			if (entry.status == status && entry.pr == pr)
				return Result<bool>::success(false);
			entry.status = status;
			entry.pr = pr;
			auto committed = commit();
			if (!committed.has_value())
				return committed;
			return Result<bool>::success(true);
		}
	return Result<bool>::failure({"REPORT.UNKNOWN_REPORT", "No such report", {}, false});
}

std::vector<std::pair<std::string, std::uint64_t>> ReportOutbox::pending_counts() const
{
	std::vector<std::pair<std::string, std::uint64_t>> counts;
	for (const auto &[fingerprint, record] : state_->fingerprints)
		if (record.pending_count > 0)
			counts.emplace_back(fingerprint, record.pending_count);
	return counts;
}

Result<bool> ReportOutbox::clear_count(const std::string &fingerprint, std::uint64_t sent)
{
	auto found = state_->fingerprints.find(fingerprint);
	if (found == state_->fingerprints.end())
		return Result<bool>::success(false);
	found->second.pending_count -= std::min(found->second.pending_count, sent);
	return commit();
}

std::vector<OutboxEntry> ReportOutbox::recent(std::size_t count) const
{
	std::vector<OutboxEntry> entries(state_->reports.rbegin(), state_->reports.rend());
	if (entries.size() > count)
		entries.resize(count);
	return entries;
}
} // namespace bug_reporter
