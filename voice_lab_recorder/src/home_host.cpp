#include "voice_lab_recorder/home_host.h"
#include "voice_lab_recorder/canonical_json.h"
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <openssl/evp.h>
#include <regex>
#include <set>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace voice_lab_recorder
{
namespace
{
using Json = nlohmann::json;
constexpr std::size_t kMaximumRecordBytes = 64 * 1024;
constexpr std::int64_t kDaySeconds = 24 * 60 * 60;

template <typename Value> Result<Value> fail(const char *code, const char *message)
{
	return Result<Value>::failure({code, message, {}, false});
}

std::string sha256_hex(const std::string &bytes)
{
	unsigned char digest[EVP_MAX_MD_SIZE];
	unsigned int size = 0;
	EVP_Digest(bytes.data(), bytes.size(), digest, &size, EVP_sha256(), nullptr);
	static const char *hex = "0123456789abcdef";
	std::string text;
	for (unsigned at = 0; at < size; ++at) {
		text += hex[digest[at] >> 4];
		text += hex[digest[at] & 15];
	}
	return text;
}

bool stable_id(const std::string &value)
{
	static const std::regex pattern("[a-z0-9](?:[a-z0-9._-]{0,126}[a-z0-9])?");
	return value.size() <= 128 && std::regex_match(value, pattern);
}

bool node_id(const std::string &value)
{
	static const std::regex pattern("[A-Za-z0-9]{1,64}");
	return std::regex_match(value, pattern);
}

bool ends_with(const std::string &text, const std::string &suffix)
{
	return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

/** A stored record: its bytes must be the canonical form of what they parse to. */
std::optional<std::pair<Json, std::string>> read_record(const std::string &path)
{
	const int descriptor = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
	if (descriptor < 0)
		return std::nullopt;
	std::string bytes;
	std::array<char, 8192> buffer{};
	bool ok = true;
	for (;;) {
		const ssize_t got = ::read(descriptor, buffer.data(), buffer.size());
		if (got < 0 && errno == EINTR)
			continue;
		if (got < 0 || bytes.size() + static_cast<std::size_t>(std::max<ssize_t>(got, 0)) > kMaximumRecordBytes) {
			ok = false;
			break;
		}
		if (got == 0)
			break;
		bytes.append(buffer.data(), static_cast<std::size_t>(got));
	}
	::close(descriptor);
	if (!ok)
		return std::nullopt;
	Json value = Json::parse(bytes, nullptr, false);
	if (value.is_discarded() || !value.is_object())
		return std::nullopt;
	const auto canonical = canonical_record(value);
	if (!canonical.has_value() || canonical.value() != bytes)
		return std::nullopt;
	return std::make_pair(value, sha256_hex(bytes));
}

bool write_new(const std::string &path, const std::string &bytes, const mode_t mode)
{
	const int descriptor = ::open(path.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, mode);
	if (descriptor < 0)
		return false;
	std::size_t done = 0;
	bool ok = true;
	while (ok && done < bytes.size()) {
		const ssize_t written = ::write(descriptor, bytes.data() + done, bytes.size() - done);
		if (written < 0 && errno == EINTR)
			continue;
		ok = written > 0;
		if (ok)
			done += static_cast<std::size_t>(written);
	}
	ok = ok && ::fsync(descriptor) == 0;
	ok = ::close(descriptor) == 0 && ok;
	if (!ok)
		::unlink(path.c_str());
	return ok;
}

bool fsync_directory(const std::string &path)
{
	const int descriptor = ::open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (descriptor < 0)
		return false;
	const bool ok = ::fsync(descriptor) == 0;
	return ::close(descriptor) == 0 && ok;
}

/** Creates an owner-only directory, or accepts one that is owned by this user with no group or other access. */
bool private_directory(const std::string &path)
{
	if (::mkdir(path.c_str(), 0700) != 0 && errno != EEXIST)
		return false;
	struct stat status {};
	return ::lstat(path.c_str(), &status) == 0 && S_ISDIR(status.st_mode) && status.st_uid == ::geteuid() &&
	       (status.st_mode & 077) == 0;
}

bool in_force(const Json &approval, const std::string &now)
{
	std::int64_t approved = 0, expires = 0, at = 0;
	return parse_utc(approval.value("approved_at", std::string()), approved) &&
	       parse_utc(approval.value("expires_at", std::string()), expires) && parse_utc(now, at) && approved <= at &&
	       at < expires;
}
} // namespace

bool parse_utc(const std::string &text, std::int64_t &seconds)
{
	static const std::regex pattern(R"((\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2}):(\d{2})(\.\d{1,9})?Z)");
	std::smatch match;
	if (!std::regex_match(text, match, pattern))
		return false;
	std::tm parts{};
	parts.tm_year = std::stoi(match[1]) - 1900;
	parts.tm_mon = std::stoi(match[2]) - 1;
	parts.tm_mday = std::stoi(match[3]);
	parts.tm_hour = std::stoi(match[4]);
	parts.tm_min = std::stoi(match[5]);
	parts.tm_sec = std::stoi(match[6]);
	if (parts.tm_mon > 11 || parts.tm_mday < 1 || parts.tm_mday > 31 || parts.tm_hour > 23 || parts.tm_min > 59 ||
	    parts.tm_sec > 59)
		return false;
	const std::tm given = parts;
	seconds = static_cast<std::int64_t>(::timegm(&parts));
	// timegm normalises an impossible date (31 February); refuse it instead.
	return parts.tm_mday == given.tm_mday && parts.tm_mon == given.tm_mon;
}

Result<Json> home_host_approval(const ApprovalFields &fields)
{
	auto invalid = [](const char *message) { return fail<Json>("VOICE_LAB.APPROVAL_INVALID", message); };
	std::string lower = fields.endpoint;
	std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
	if (!stable_id(fields.approval_id))
		return invalid("The approval id is not a stable id");
	if (fields.endpoint.empty() || fields.endpoint.size() > 1024 || lower != fields.endpoint ||
	    fields.tailnet_suffix.empty() || fields.tailnet_suffix.size() > 253 ||
	    !ends_with(fields.endpoint, "." + fields.tailnet_suffix))
		return invalid("The endpoint must be a lower-case name in the approved tailnet");
	if (!node_id(fields.node_stable_id))
		return invalid("A node's stable id is 1 to 64 letters and digits");
	const std::set<std::string> allowed = {"capture", "measurement"};
	if (fields.classes.empty() || fields.classes.size() > 2 ||
	    !std::is_sorted(fields.classes.begin(), fields.classes.end()) ||
	    std::adjacent_find(fields.classes.begin(), fields.classes.end()) != fields.classes.end() ||
	    std::any_of(fields.classes.begin(), fields.classes.end(),
			[&allowed](const std::string &value) { return allowed.count(value) == 0; }))
		return invalid("Classes are capture or measurement, sorted and unique");
	std::int64_t approved = 0, expires = 0;
	if (!parse_utc(fields.approved_at, approved) || !parse_utc(fields.expires_at, expires) || expires <= approved ||
	    expires - approved > kMaximumApprovalDays * kDaySeconds)
		return invalid("An approval stands after it is made, for at most 366 days");
	if (fields.approved_by.empty() || fields.approved_by.size() > 256 || fields.statement.empty() ||
	    fields.statement.size() > 4096)
		return invalid("The approval names who approved it and what it allows");
	return Result<Json>::success({{"schema_version", "cole-voice-lab.home-host-approval/v1"},
				      {"approval_id", fields.approval_id},
				      {"approved", true},
				      {"endpoint", fields.endpoint},
				      {"tailnet_suffix", fields.tailnet_suffix},
				      {"node_stable_id", fields.node_stable_id},
				      {"tag", kHomeHostTag},
				      {"classes", fields.classes},
				      {"approved_at", fields.approved_at},
				      {"expires_at", fields.expires_at},
				      {"approved_by", fields.approved_by},
				      {"statement", fields.statement}});
}

Result<std::string> record_sha256(const Json &record)
{
	auto bytes = canonical_record(record);
	if (!bytes.has_value())
		return bytes;
	return Result<std::string>::success(sha256_hex(bytes.value()));
}

Result<bool> check_peer(const Json &approval, const PeerIdentity &peer)
{
	auto mismatch = [](const char *message) {
		return fail<bool>("VOICE_LAB.HOME_HOST_IDENTITY_MISMATCH", message);
	};
	const std::string endpoint = approval.value("endpoint", std::string());
	if (!ends_with(endpoint, "." + approval.value("tailnet_suffix", std::string())))
		return mismatch("The endpoint is not in the approved tailnet");
	if (peer.node_stable_id != approval.value("node_stable_id", std::string()))
		return mismatch("The node answering is not the approved node");
	if (std::find(peer.tags.begin(), peer.tags.end(), kHomeHostTag) == peer.tags.end())
		return mismatch("The node does not carry tag:voice-lab");
	std::string name = peer.dns_name;
	if (!name.empty() && name.back() == '.')
		name.pop_back();
	if (name != endpoint)
		return mismatch("The node's own name is not the endpoint");
	return Result<bool>::success(true);
}

HomeHost::HomeHost(std::string directory) : directory_(std::move(directory)) {}

Result<Json> HomeHost::approve(const Json &approval, const std::string &reviewed_sha256, const std::string &now)
{
	const auto digest = record_sha256(approval);
	if (!digest.has_value() || digest.value() != reviewed_sha256)
		return fail<Json>("VOICE_LAB.APPROVAL_DIGEST_MISMATCH", "The reviewed digest is not this approval's");
	if (!in_force(approval, now))
		return fail<Json>("VOICE_LAB.APPROVAL_NOT_IN_FORCE", "The approval does not stand now");
	const std::string id = approval.value("approval_id", std::string());
	if (!stable_id(id) || !private_directory(directory_) || !private_directory(directory_ + "/approvals"))
		return fail<Json>("VOICE_LAB.HOME_HOST_STORE_UNSAFE", "The home-host directory is not owner-only");
	const std::string path = directory_ + "/approvals/" + id + ".json";
	struct stat existing {};
	if (::lstat(path.c_str(), &existing) == 0)
		return fail<Json>("VOICE_LAB.APPROVAL_EXISTS", "Approvals are never replaced; use a new id");
	if (!write_new(path, canonical_record(approval).value(), 0400) || !fsync_directory(directory_ + "/approvals"))
		return fail<Json>("VOICE_LAB.HOME_HOST_WRITE_FAILED", "Cannot store the approval");
	const Json binding = {{"schema_version", "cole-voice-lab.home-host-binding/v1"},
			      {"approval_id", id},
			      {"approval_sha256", digest.value()},
			      {"bound_at", now}};
	const std::string replacement = directory_ + "/.binding.json." + std::to_string(::getpid());
	::unlink(replacement.c_str());
	if (!write_new(replacement, canonical_record(binding).value(), 0600) ||
	    ::rename(replacement.c_str(), (directory_ + "/binding.json").c_str()) != 0 || !fsync_directory(directory_)) {
		::unlink(replacement.c_str());
		return fail<Json>("VOICE_LAB.HOME_HOST_WRITE_FAILED", "Cannot bind the approval");
	}
	return Result<Json>::success(binding);
}

Result<BoundApproval> HomeHost::load(const std::string &now) const
{
	struct stat status {};
	if (::lstat((directory_ + "/binding.json").c_str(), &status) != 0)
		return fail<BoundApproval>("VOICE_LAB.APPROVAL_MISSING", "No home-host approval is bound");
	const auto binding = read_record(directory_ + "/binding.json");
	const std::string id = binding ? binding->first.value("approval_id", std::string()) : std::string();
	if (!binding || !stable_id(id))
		return fail<BoundApproval>("VOICE_LAB.APPROVAL_TAMPERED", "The binding is unreadable or not canonical");
	const auto approval = read_record(directory_ + "/approvals/" + id + ".json");
	if (!approval || approval->second != binding->first.value("approval_sha256", std::string()) ||
	    approval->first.value("approval_id", std::string()) != id)
		return fail<BoundApproval>("VOICE_LAB.APPROVAL_TAMPERED", "The approval is not the one that was bound");
	if (!in_force(approval->first, now))
		return fail<BoundApproval>("VOICE_LAB.APPROVAL_NOT_IN_FORCE",
					   "The approval does not stand now; approve the PC again");
	return Result<BoundApproval>::success({approval->first, approval->second});
}

Result<std::vector<std::pair<Json, std::string>>> HomeHost::entries() const
{
	using Output = Result<std::vector<std::pair<Json, std::string>>>;
	const std::string ledger = directory_ + "/ledger";
	std::vector<std::string> names;
	if (DIR *handle = ::opendir(ledger.c_str())) {
		while (const dirent *entry = ::readdir(handle))
			if (entry->d_name[0] != '.')
				names.emplace_back(entry->d_name);
		::closedir(handle);
	}
	std::sort(names.begin(), names.end());
	std::vector<std::pair<Json, std::string>> found;
	for (std::size_t index = 1; index <= names.size(); ++index) {
		char expected[32];
		std::snprintf(expected, sizeof expected, "%08zu.json", index);
		if (names[index - 1] != expected)
			return fail<std::vector<std::pair<Json, std::string>>>("VOICE_LAB.LEDGER_BROKEN",
									       "The ledger has a gap or a stray file");
		const auto entry = read_record(ledger + "/" + names[index - 1]);
		const Json previous = found.empty() ? Json(nullptr) : Json(found.back().second);
		if (!entry || entry->first.value("sequence", std::uint64_t{0}) != index ||
		    entry->first.value("previous_sha256", Json()) != previous)
			return fail<std::vector<std::pair<Json, std::string>>>("VOICE_LAB.LEDGER_BROKEN",
									       "The ledger's chain is broken");
		found.push_back(*entry);
	}
	return Output::success(std::move(found));
}

Result<Json> HomeHost::append(const BoundApproval &approval, const LedgerEvent &event)
{
	const bool completed = event.event == "upload-completed";
	if ((event.event != "upload-started" && !completed && event.event != "upload-refused") ||
	    completed == event.receipt_sha256.empty() || event.detail.size() > 2000 || !stable_id(event.session_id))
		return fail<Json>("VOICE_LAB.LEDGER_ENTRY_INVALID", "Exactly a completed upload names its receipt");
	const std::string ledger = directory_ + "/ledger";
	if (!private_directory(directory_) || !private_directory(ledger))
		return fail<Json>("VOICE_LAB.HOME_HOST_STORE_UNSAFE", "The home-host directory is not owner-only");
	const int lock = ::open(ledger.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
	if (lock < 0 || ::flock(lock, LOCK_EX) != 0) {
		if (lock >= 0)
			::close(lock);
		return fail<Json>("VOICE_LAB.HOME_HOST_WRITE_FAILED", "Cannot lock the ledger");
	}
	auto existing = entries();
	Result<Json> outcome = fail<Json>("VOICE_LAB.HOME_HOST_WRITE_FAILED", "Cannot write the ledger entry");
	if (!existing.has_value()) {
		outcome = Result<Json>::failure(existing.error());
	} else {
		const auto sequence = existing.value().size() + 1;
		const Json entry = {
			{"schema_version", "cole-voice-lab.home-host-ledger-entry/v1"},
			{"sequence", sequence},
			{"previous_sha256", existing.value().empty() ? Json(nullptr) : Json(existing.value().back().second)},
			{"event", event.event},
			{"at", event.at},
			{"approval_id", approval.approval.value("approval_id", std::string())},
			{"approval_sha256", approval.sha256},
			{"endpoint", approval.approval.value("endpoint", std::string())},
			{"node_stable_id", approval.approval.value("node_stable_id", std::string())},
			{"session_id", event.session_id},
			{"manifest_sha256", event.manifest_sha256},
			{"receipt_sha256", completed ? Json(event.receipt_sha256) : Json(nullptr)},
			{"detail", event.detail.empty() ? Json(nullptr) : Json(event.detail)},
		};
		char name[32];
		std::snprintf(name, sizeof name, "/%08zu.json", sequence);
		const auto bytes = canonical_record(entry);
		if (bytes.has_value() && write_new(ledger + name, bytes.value(), 0400) && fsync_directory(ledger))
			outcome = Result<Json>::success(entry);
	}
	::close(lock);
	return outcome;
}
} // namespace voice_lab_recorder
