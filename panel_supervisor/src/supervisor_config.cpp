#include "panel_supervisor/supervisor_config.hpp"

#include "panel_supervisor/error_code.hpp"

#include <map>
#include <string>
#include <utility>

namespace house_panel::supervisor {
namespace {

void record_invalid_brightness(std::map<std::string, std::string> &context, const char *field_name,
			       const int value)
{
	if (value < 0 || value > 100) {
		context.emplace(field_name, std::to_string(value));
	}
}

} // namespace

Result<SupervisorConfig> SupervisorConfig::create(const std::chrono::milliseconds grace_period,
						  const BrightnessLevels brightness_levels)
{
	std::map<std::string, std::string> invalid_fields;
	if (grace_period.count() < 0) {
		invalid_fields.emplace("grace_period_ms", std::to_string(grace_period.count()));
	}
	record_invalid_brightness(invalid_fields, "present_day_percent",
				  brightness_levels.present_day_percent);
	record_invalid_brightness(invalid_fields, "present_night_percent",
				  brightness_levels.present_night_percent);
	record_invalid_brightness(invalid_fields, "grace_day_percent",
				  brightness_levels.grace_day_percent);
	record_invalid_brightness(invalid_fields, "grace_night_percent",
				  brightness_levels.grace_night_percent);
	if (brightness_levels.suspended_percent != 0) {
		invalid_fields.emplace("suspended_percent",
				       "must_be_exactly_0_for_boot_and_suspend_safety");
	}

	if (!invalid_fields.empty()) {
		return Result<SupervisorConfig>::failure(
			SupervisorError(ErrorCode::InvalidConfiguration,
					"Failed to create panel supervisor configuration because "
					"one or more fields are invalid",
					std::move(invalid_fields)));
	}

	return Result<SupervisorConfig>::success(SupervisorConfig(grace_period, brightness_levels));
}

SupervisorConfig::SupervisorConfig(const std::chrono::milliseconds grace_period,
				   const BrightnessLevels brightness_levels) noexcept
    : grace_period_(grace_period), brightness_levels_(brightness_levels)
{
}

std::chrono::milliseconds SupervisorConfig::grace_period() const noexcept { return grace_period_; }

const BrightnessLevels &SupervisorConfig::brightness_levels() const noexcept
{
	return brightness_levels_;
}

} // namespace house_panel::supervisor
