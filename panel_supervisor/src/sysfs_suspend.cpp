#include "panel_supervisor/sysfs_suspend.hpp"

#include "exact_file_writer.hpp"
#include "panel_supervisor/error_code.hpp"

#include <map>
#include <string>
#include <utility>

namespace house_panel::supervisor {
namespace {

bool is_explicit_power_state_path(const std::string &path) noexcept
{
	return path == "/sys/power/state";
}

} // namespace

Result<SysfsSuspend> SysfsSuspend::create(std::string power_state_path, std::string suspend_mode)
{
	std::map<std::string, std::string> invalid_fields;
	if (!is_explicit_power_state_path(power_state_path)) {
		invalid_fields.emplace("power_state_path",
				       "must_be_exactly_/sys/power/state");
	}
	if (suspend_mode != "freeze" && suspend_mode != "mem") {
		invalid_fields.emplace("suspend_mode", "must_be_exactly_freeze_or_mem");
	}
	if (!invalid_fields.empty()) {
		return Result<SysfsSuspend>::failure(SupervisorError(
			ErrorCode::SuspendConfigurationInvalid,
			"Failed to create sysfs suspend adapter because configuration is invalid",
			std::move(invalid_fields)));
	}
	return Result<SysfsSuspend>::success(
		SysfsSuspend(std::move(power_state_path), std::move(suspend_mode)));
}

SysfsSuspend::SysfsSuspend(std::string power_state_path, std::string suspend_mode)
    : power_state_path_(std::move(power_state_path)), suspend_mode_(std::move(suspend_mode))
{
}

Result<void> SysfsSuspend::request_suspend()
{
	return detail::write_exact_file(power_state_path_, suspend_mode_,
					ErrorCode::PowerStateWriteFailed,
					"sysfs_suspend.request_suspend");
}

const std::string &SysfsSuspend::power_state_path() const noexcept { return power_state_path_; }

const std::string &SysfsSuspend::suspend_mode() const noexcept { return suspend_mode_; }

} // namespace house_panel::supervisor
