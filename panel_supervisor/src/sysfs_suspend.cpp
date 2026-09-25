#include "panel_supervisor/sysfs_suspend.hpp"

#include "exact_file_writer.hpp"
#include "panel_supervisor/error_code.hpp"
#include "sysfs_mem_sleep_access.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <fcntl.h>
#include <map>
#include <string>
#include <string_view>
#include <unistd.h>
#include <utility>

namespace house_panel::supervisor {
namespace {

constexpr std::size_t kMaximumMemSleepTextBytes = 64;
constexpr std::array<std::string_view, 3> kMemSleepLabels{"s2idle", "shallow", "deep"};

bool is_explicit_power_state_path(const std::string &path) noexcept
{
	return path == "/sys/power/state";
}

bool is_explicit_mem_sleep_path(const std::string &path) noexcept
{
	return path == "/sys/power/mem_sleep";
}

bool is_mem_sleep_label(const std::string_view label) noexcept
{
	return std::find(kMemSleepLabels.begin(), kMemSleepLabels.end(), label) !=
	       kMemSleepLabels.end();
}

SupervisorError mem_sleep_failure(const std::string &path, const char *failure_stage,
				  const int error_number,
				  std::map<std::string, std::string> extra_context = {})
{
	extra_context.emplace("errno", std::to_string(error_number));
	extra_context.emplace("failure_stage", failure_stage);
	extra_context.emplace("operation", "sysfs_suspend.select_mem_sleep");
	extra_context.emplace("path", path);
	return SupervisorError(ErrorCode::MemSleepSelectionFailed,
			       "Failed to select and confirm the configured mem_sleep variant",
			       std::move(extra_context),
			       error_number == EINTR || error_number == EAGAIN ||
				       error_number == EBUSY);
}

Result<std::string> read_mem_sleep_text(const std::string &path)
{
	int open_flags = O_RDONLY;
#ifdef O_CLOEXEC
	open_flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
	open_flags |= O_NOFOLLOW;
#endif
	const int file_descriptor = ::open(path.c_str(), open_flags);
	if (file_descriptor < 0) {
		return Result<std::string>::failure(mem_sleep_failure(path, "open", errno));
	}
	std::array<char, kMaximumMemSleepTextBytes + 1> buffer{};
	std::size_t bytes_read = 0;
	while (bytes_read < buffer.size()) {
		const ssize_t read_result = ::read(file_descriptor, buffer.data() + bytes_read,
						   buffer.size() - bytes_read);
		if (read_result < 0 && errno == EINTR) {
			continue;
		}
		if (read_result < 0) {
			const int read_error_number = errno;
			(void)::close(file_descriptor);
			return Result<std::string>::failure(
				mem_sleep_failure(path, "read", read_error_number));
		}
		if (read_result == 0) {
			break;
		}
		bytes_read += static_cast<std::size_t>(read_result);
	}
	if (::close(file_descriptor) != 0) {
		return Result<std::string>::failure(mem_sleep_failure(path, "close", errno));
	}
	if (bytes_read > kMaximumMemSleepTextBytes) {
		return Result<std::string>::failure(mem_sleep_failure(
			path, "read", 0, {{"observed_text_bytes", "more_than_64"}}));
	}
	return Result<std::string>::success(std::string(buffer.data(), bytes_read));
}

std::string join_labels(const std::vector<std::string> &labels)
{
	std::string joined;
	for (const auto &label : labels) {
		if (!joined.empty()) {
			joined += ',';
		}
		joined += label;
	}
	return joined;
}

} // namespace

namespace detail {

Result<MemSleepVariants> parse_mem_sleep_text(std::string_view text)
{
	auto parse_failure = [&text](const char *reason) {
		return Result<MemSleepVariants>::failure(SupervisorError(
			ErrorCode::MemSleepSelectionFailed,
			"Failed to parse the kernel mem_sleep variant list",
			{{"failure_stage", "parse"},
			 {"operation", "sysfs_suspend.parse_mem_sleep"},
			 {"observed_text_bytes", std::to_string(text.size())},
			 {"reason", reason}}));
	};
	if (!text.empty() && text.back() == '\n') {
		text.remove_suffix(1);
	}
	if (text.empty()) {
		return parse_failure("empty");
	}
	MemSleepVariants variants;
	std::size_t selected_count = 0;
	std::size_t token_start = 0;
	while (token_start <= text.size()) {
		const std::size_t separator = text.find(' ', token_start);
		const std::size_t token_end =
			separator == std::string_view::npos ? text.size() : separator;
		std::string_view token = text.substr(token_start, token_end - token_start);
		bool bracketed = false;
		if (token.size() >= 2 && token.front() == '[' && token.back() == ']') {
			bracketed = true;
			token = token.substr(1, token.size() - 2);
		}
		if (!is_mem_sleep_label(token)) {
			return parse_failure("unknown_or_malformed_label");
		}
		if (std::find(variants.available.begin(), variants.available.end(), token) !=
		    variants.available.end()) {
			return parse_failure("duplicate_label");
		}
		variants.available.emplace_back(token);
		if (bracketed) {
			++selected_count;
			variants.selected = std::string(token);
		}
		if (separator == std::string_view::npos) {
			break;
		}
		token_start = separator + 1;
	}
	if (selected_count != 1) {
		return parse_failure("selection_count_not_exactly_one");
	}
	return Result<MemSleepVariants>::success(std::move(variants));
}

Result<void> select_mem_sleep_state_at_path(const std::string &mem_sleep_path,
					    const std::string &requested_state)
{
	auto observed_text = read_mem_sleep_text(mem_sleep_path);
	if (!observed_text) {
		return Result<void>::failure(observed_text.error());
	}
	auto observed = parse_mem_sleep_text(observed_text.value());
	if (!observed) {
		return Result<void>::failure(observed.error());
	}
	if (observed.value().selected == requested_state) {
		return Result<void>::success();
	}
	const auto &available = observed.value().available;
	if (std::find(available.begin(), available.end(), requested_state) == available.end()) {
		return Result<void>::failure(SupervisorError(
			ErrorCode::MemSleepStateUnavailable,
			"Configured mem_sleep variant is not offered by this kernel and board",
			{{"available", join_labels(available)},
			 {"operation", "sysfs_suspend.select_mem_sleep"},
			 {"path", mem_sleep_path},
			 {"requested", requested_state},
			 {"selected", observed.value().selected}}));
	}
	const auto write_result =
		write_exact_file(mem_sleep_path, requested_state, ErrorCode::MemSleepSelectionFailed,
				 "sysfs_suspend.select_mem_sleep");
	if (!write_result) {
		return write_result;
	}
	auto confirmed_text = read_mem_sleep_text(mem_sleep_path);
	if (!confirmed_text) {
		return Result<void>::failure(confirmed_text.error());
	}
	auto confirmed = parse_mem_sleep_text(confirmed_text.value());
	if (!confirmed || confirmed.value().selected != requested_state) {
		return Result<void>::failure(mem_sleep_failure(
			mem_sleep_path, "confirm", 0,
			{{"requested", requested_state},
			 {"selected", confirmed ? confirmed.value().selected : "unparseable"}}));
	}
	return Result<void>::success();
}

} // namespace detail

Result<SysfsSuspend> SysfsSuspend::create(std::string power_state_path, std::string suspend_mode,
					  std::optional<MemSleepSelection> mem_sleep_selection)
{
	std::map<std::string, std::string> invalid_fields;
	if (!is_explicit_power_state_path(power_state_path)) {
		invalid_fields.emplace("power_state_path",
				       "must_be_exactly_/sys/power/state");
	}
	if (suspend_mode != "freeze" && suspend_mode != "mem") {
		invalid_fields.emplace("suspend_mode", "must_be_exactly_freeze_or_mem");
	}
	if (suspend_mode == "mem" && !mem_sleep_selection.has_value()) {
		invalid_fields.emplace("mem_sleep_state", "required_when_suspend_mode_is_mem");
	}
	if (suspend_mode != "mem" && mem_sleep_selection.has_value()) {
		invalid_fields.emplace("mem_sleep_state", "only_valid_when_suspend_mode_is_mem");
	}
	if (mem_sleep_selection.has_value()) {
		if (!is_explicit_mem_sleep_path(mem_sleep_selection->mem_sleep_path)) {
			invalid_fields.emplace("mem_sleep_path",
					       "must_be_exactly_/sys/power/mem_sleep");
		}
		if (!is_mem_sleep_label(mem_sleep_selection->mem_sleep_state)) {
			invalid_fields.emplace("mem_sleep_state",
					       "must_be_exactly_s2idle_shallow_or_deep");
		}
	}
	if (!invalid_fields.empty()) {
		return Result<SysfsSuspend>::failure(SupervisorError(
			ErrorCode::SuspendConfigurationInvalid,
			"Failed to create sysfs suspend adapter because configuration is invalid",
			std::move(invalid_fields)));
	}
	return Result<SysfsSuspend>::success(SysfsSuspend(
		std::move(power_state_path), std::move(suspend_mode), std::move(mem_sleep_selection)));
}

SysfsSuspend::SysfsSuspend(std::string power_state_path, std::string suspend_mode,
			   std::optional<MemSleepSelection> mem_sleep_selection)
    : power_state_path_(std::move(power_state_path)), suspend_mode_(std::move(suspend_mode)),
      mem_sleep_selection_(std::move(mem_sleep_selection))
{
}

Result<void> SysfsSuspend::request_suspend()
{
	if (mem_sleep_selection_.has_value()) {
		const auto selection_result = detail::select_mem_sleep_state_at_path(
			mem_sleep_selection_->mem_sleep_path, mem_sleep_selection_->mem_sleep_state);
		if (!selection_result) {
			return selection_result;
		}
	}
	return detail::write_exact_file(power_state_path_, suspend_mode_,
					ErrorCode::PowerStateWriteFailed,
					"sysfs_suspend.request_suspend");
}

const std::string &SysfsSuspend::power_state_path() const noexcept { return power_state_path_; }

const std::string &SysfsSuspend::suspend_mode() const noexcept { return suspend_mode_; }

const std::optional<MemSleepSelection> &SysfsSuspend::mem_sleep_selection() const noexcept
{
	return mem_sleep_selection_;
}

} // namespace house_panel::supervisor
