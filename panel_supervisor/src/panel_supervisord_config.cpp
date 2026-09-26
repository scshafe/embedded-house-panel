#include "panel_supervisor/panel_supervisord_config.hpp"

#include "panel_supervisor/brightness_levels.hpp"
#include "panel_supervisor/error_code.hpp"
#include "panel_supervisor/evdev_presence_source.hpp"
#include "panel_supervisor/sysfs_backlight.hpp"
#include "panel_supervisor/sysfs_suspend.hpp"

#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <locale>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

namespace house_panel::supervisor {
namespace {

constexpr const char *kLatitude = "BHP_PANEL_SUPERVISOR_LATITUDE";
constexpr const char *kLongitude = "BHP_PANEL_SUPERVISOR_LONGITUDE";
constexpr const char *kPresenceGraceSeconds = "BHP_PANEL_SUPERVISOR_PRESENCE_GRACE_SECONDS";
constexpr const char *kPresenceInputPath = "BHP_PANEL_SUPERVISOR_PRESENCE_INPUT_PATH";
constexpr const char *kPresenceInputName = "BHP_PANEL_SUPERVISOR_PRESENCE_INPUT_NAME";
constexpr const char *kBacklightPath = "BHP_PANEL_SUPERVISOR_BACKLIGHT_PATH";
constexpr const char *kBacklightMaximumPath = "BHP_PANEL_SUPERVISOR_BACKLIGHT_MAX_PATH";
constexpr const char *kModeSocket = "BHP_PANEL_SUPERVISOR_MODE_SOCKET";
constexpr const char *kPanelUiUid = "BHP_PANEL_SUPERVISOR_PANEL_UI_UID";
constexpr const char *kPresentationTimeoutMilliseconds =
	"BHP_PANEL_SUPERVISOR_PRESENTATION_TIMEOUT_MILLISECONDS";
constexpr const char *kDayBrightness = "BHP_PANEL_SUPERVISOR_DAY_BRIGHTNESS";
constexpr const char *kNightBrightness = "BHP_PANEL_SUPERVISOR_NIGHT_BRIGHTNESS";
constexpr const char *kGraceDayBrightness = "BHP_PANEL_SUPERVISOR_GRACE_DAY_BRIGHTNESS";
constexpr const char *kGraceNightBrightness = "BHP_PANEL_SUPERVISOR_GRACE_NIGHT_BRIGHTNESS";
constexpr const char *kOffBrightness = "BHP_PANEL_SUPERVISOR_OFF_BRIGHTNESS";
constexpr const char *kSuspendMode = "BHP_PANEL_SUPERVISOR_SUSPEND_MODE";
constexpr const char *kPowerStatePath = "BHP_PANEL_SUPERVISOR_POWER_STATE_PATH";
constexpr const char *kMemSleepPath = "BHP_PANEL_SUPERVISOR_MEM_SLEEP_PATH";
constexpr const char *kMemSleepState = "BHP_PANEL_SUPERVISOR_MEM_SLEEP_STATE";
constexpr const char *kPollIntervalMilliseconds = "BHP_PANEL_SUPERVISOR_POLL_INTERVAL_MILLISECONDS";
constexpr const char *kSuspendRetryBackoffMilliseconds =
	"BHP_PANEL_SUPERVISOR_SUSPEND_RETRY_BACKOFF_MILLISECONDS";

constexpr long long kMinimumPollIntervalMilliseconds = 50;
constexpr long long kMaximumPollIntervalMilliseconds = 5'000;
constexpr long long kMinimumSuspendRetryBackoffMilliseconds = 1'000;
constexpr long long kMaximumSuspendRetryBackoffMilliseconds = 300'000;

constexpr long long kMinimumPresentationTimeoutMilliseconds = 1;
constexpr long long kMaximumPresentationTimeoutMilliseconds = 60'000;

constexpr std::array<const char *, 21> kKnownEnvironmentVariables{
	kLatitude,
	kLongitude,
	kPresenceGraceSeconds,
	kPresenceInputPath,
	kPresenceInputName,
	kBacklightPath,
	kBacklightMaximumPath,
	kModeSocket,
	kPanelUiUid,
	kPresentationTimeoutMilliseconds,
	kDayBrightness,
	kNightBrightness,
	kGraceDayBrightness,
	kGraceNightBrightness,
	kOffBrightness,
	kSuspendMode,
	kPowerStatePath,
	kMemSleepPath,
	kMemSleepState,
	kPollIntervalMilliseconds,
	kSuspendRetryBackoffMilliseconds,
};

const std::string *find_field(const std::map<std::string, std::string> &fields,
			      const char *field_name)
{
	const auto field = fields.find(field_name);
	return field == fields.end() ? nullptr : &field->second;
}

const std::string *require_field(const std::map<std::string, std::string> &fields,
				 const char *field_name,
				 std::map<std::string, std::string> &invalid_fields)
{
	const std::string *value = find_field(fields, field_name);
	if (value == nullptr || value->empty()) {
		invalid_fields.emplace(field_name, "missing_or_empty");
		return nullptr;
	}
	return value;
}

bool parse_decimal_double(const std::string &text, double &value)
{
	std::istringstream input(text);
	input.imbue(std::locale::classic());
	input >> std::noskipws >> value;
	if (input.fail() || !std::isfinite(value)) {
		return false;
	}
	char trailing_character = '\0';
	return !(input >> trailing_character) && input.eof();
}

bool parse_decimal_integer(const std::string &text, long long &value)
{
	const auto parse_result = std::from_chars(text.data(), text.data() + text.size(), value);
	return parse_result.ec == std::errc{} && parse_result.ptr == text.data() + text.size();
}

bool is_explicit_path(const std::string &path) noexcept
{
	return !path.empty() && path.front() == '/' &&
	       path.find("REPLACE_ME") == std::string::npos &&
	       path.find('\n') == std::string::npos && path.find('\r') == std::string::npos;
}

void merge_error_context(std::map<std::string, std::string> &invalid_fields,
			 const SupervisorError &error)
{
	for (const auto &[key, value] : error.context()) {
		invalid_fields.emplace(key, value);
	}
}

} // namespace

Result<PanelSupervisordConfig> PanelSupervisordConfig::from_environment()
{
	std::map<std::string, std::string> configuration_fields;
	for (const char *field_name : kKnownEnvironmentVariables) {
		const char *value = std::getenv(field_name);
		if (value != nullptr) {
			configuration_fields.emplace(field_name, value);
		}
	}
	for (const char *field_name : kTouchRuntimeEnvironmentFields) {
		const char *value = std::getenv(field_name);
		if (value != nullptr) configuration_fields.emplace(field_name, value);
	}
	return create(configuration_fields);
}

Result<PanelSupervisordConfig>
PanelSupervisordConfig::create(const std::map<std::string, std::string> &configuration_fields)
{
	std::map<std::string, std::string> invalid_fields;
	auto touch_result = TouchRuntimeConfiguration::parse(configuration_fields);
	if (!touch_result) merge_error_context(invalid_fields, touch_result.error());
	const std::string *latitude_text =
		require_field(configuration_fields, kLatitude, invalid_fields);
	const std::string *longitude_text =
		require_field(configuration_fields, kLongitude, invalid_fields);
	const std::string *grace_seconds_text =
		require_field(configuration_fields, kPresenceGraceSeconds, invalid_fields);
	const std::string *presence_path = find_field(configuration_fields, kPresenceInputPath);
	const std::string *presence_name = find_field(configuration_fields, kPresenceInputName);
	const bool has_presence_path = presence_path != nullptr && !presence_path->empty();
	const bool has_presence_name = presence_name != nullptr && !presence_name->empty();
	if (has_presence_path == has_presence_name) {
		invalid_fields[kPresenceInputPath] =
			"exactly_one_of_presence_input_path_or_presence_input_name_is_required";
		invalid_fields[kPresenceInputName] =
			"exactly_one_of_presence_input_path_or_presence_input_name_is_required";
	}
	const std::string *backlight_path =
		require_field(configuration_fields, kBacklightPath, invalid_fields);
	const std::string *backlight_maximum_path =
		require_field(configuration_fields, kBacklightMaximumPath, invalid_fields);
	const std::string *mode_socket_path =
		require_field(configuration_fields, kModeSocket, invalid_fields);
	const std::string *panel_ui_uid_text =
		require_field(configuration_fields, kPanelUiUid, invalid_fields);
	const std::string *presentation_timeout_text =
		require_field(configuration_fields, kPresentationTimeoutMilliseconds,
			      invalid_fields);
	const std::string *day_brightness_text =
		require_field(configuration_fields, kDayBrightness, invalid_fields);
	const std::string *night_brightness_text =
		require_field(configuration_fields, kNightBrightness, invalid_fields);
	const std::string *grace_day_brightness_text =
		require_field(configuration_fields, kGraceDayBrightness, invalid_fields);
	const std::string *grace_night_brightness_text =
		require_field(configuration_fields, kGraceNightBrightness, invalid_fields);
	const std::string *off_brightness_text =
		require_field(configuration_fields, kOffBrightness, invalid_fields);
	const std::string *suspend_mode_text =
		require_field(configuration_fields, kSuspendMode, invalid_fields);
	const std::string *poll_interval_text =
		require_field(configuration_fields, kPollIntervalMilliseconds, invalid_fields);

	double latitude_degrees = 0.0;
	double longitude_degrees = 0.0;
	if (latitude_text != nullptr && (!parse_decimal_double(*latitude_text, latitude_degrees) ||
					 latitude_degrees < -90.0 || latitude_degrees > 90.0)) {
		invalid_fields[kLatitude] = "must_be_decimal_between_-90_and_90";
	}
	if (longitude_text != nullptr &&
	    (!parse_decimal_double(*longitude_text, longitude_degrees) ||
	     longitude_degrees < -180.0 || longitude_degrees > 180.0)) {
		invalid_fields[kLongitude] = "must_be_decimal_between_-180_and_180";
	}

	long long grace_seconds = 0;
	if (grace_seconds_text != nullptr &&
	    (!parse_decimal_integer(*grace_seconds_text, grace_seconds) || grace_seconds < 0 ||
	     grace_seconds > std::numeric_limits<long long>::max() / 1000)) {
		invalid_fields[kPresenceGraceSeconds] = "must_be_non_negative_decimal_seconds";
	}
	long long poll_interval_milliseconds = 0;
	if (poll_interval_text != nullptr &&
	    (!parse_decimal_integer(*poll_interval_text, poll_interval_milliseconds) ||
	     poll_interval_milliseconds < kMinimumPollIntervalMilliseconds ||
	     poll_interval_milliseconds > kMaximumPollIntervalMilliseconds)) {
		invalid_fields[kPollIntervalMilliseconds] =
			"must_be_decimal_milliseconds_between_50_and_5000";
	}
	long long panel_ui_uid = 0;
	if (panel_ui_uid_text != nullptr &&
	    (!parse_decimal_integer(*panel_ui_uid_text, panel_ui_uid) || panel_ui_uid < 1 ||
	     static_cast<unsigned long long>(panel_ui_uid) >
		     std::numeric_limits<std::uint32_t>::max())) {
		invalid_fields[kPanelUiUid] = "must_be_decimal_uid_between_1_and_4294967295";
	}
	long long presentation_timeout_milliseconds = 0;
	if (presentation_timeout_text != nullptr &&
	    (!parse_decimal_integer(*presentation_timeout_text,
				    presentation_timeout_milliseconds) ||
	     presentation_timeout_milliseconds < kMinimumPresentationTimeoutMilliseconds ||
	     presentation_timeout_milliseconds > kMaximumPresentationTimeoutMilliseconds)) {
		invalid_fields[kPresentationTimeoutMilliseconds] =
			"must_be_decimal_milliseconds_between_1_and_60000";
	}

	auto parse_brightness = [&invalid_fields](const char *field_name,
						  const std::string *text) -> int {
		long long parsed_value = 0;
		if (text == nullptr || !parse_decimal_integer(*text, parsed_value) ||
		    parsed_value < 0 || parsed_value > 100) {
			if (text != nullptr) {
				invalid_fields[field_name] =
					"must_be_decimal_percent_between_0_and_100";
			}
			return 0;
		}
		return static_cast<int>(parsed_value);
	};
	const int day_brightness = parse_brightness(kDayBrightness, day_brightness_text);
	const int night_brightness = parse_brightness(kNightBrightness, night_brightness_text);
	const int grace_day_brightness =
		parse_brightness(kGraceDayBrightness, grace_day_brightness_text);
	const int grace_night_brightness =
		parse_brightness(kGraceNightBrightness, grace_night_brightness_text);
	const int off_brightness = parse_brightness(kOffBrightness, off_brightness_text);
	if (off_brightness_text != nullptr &&
	    invalid_fields.find(kOffBrightness) == invalid_fields.end() && off_brightness != 0) {
		invalid_fields[kOffBrightness] = "must_be_exactly_0_for_boot_and_suspend_safety";
	}

	SuspendMode suspend_mode = SuspendMode::Disabled;
	if (suspend_mode_text != nullptr) {
		if (*suspend_mode_text == "disabled") {
			suspend_mode = SuspendMode::Disabled;
		} else if (*suspend_mode_text == "freeze") {
			suspend_mode = SuspendMode::Freeze;
		} else if (*suspend_mode_text == "mem") {
			suspend_mode = SuspendMode::Mem;
		} else {
			invalid_fields[kSuspendMode] = "must_be_disabled_freeze_or_mem";
		}
	}
	const std::string *power_state_path = find_field(configuration_fields, kPowerStatePath);
	const std::string *suspend_retry_backoff_text =
		find_field(configuration_fields, kSuspendRetryBackoffMilliseconds);
	if (suspend_mode != SuspendMode::Disabled &&
	    (power_state_path == nullptr || power_state_path->empty())) {
		invalid_fields[kPowerStatePath] = "required_when_suspend_mode_is_enabled";
	}
	const std::string *mem_sleep_path = find_field(configuration_fields, kMemSleepPath);
	const std::string *mem_sleep_state = find_field(configuration_fields, kMemSleepState);
	const bool has_mem_sleep_path = mem_sleep_path != nullptr && !mem_sleep_path->empty();
	const bool has_mem_sleep_state = mem_sleep_state != nullptr && !mem_sleep_state->empty();
	if (suspend_mode == SuspendMode::Mem) {
		if (!has_mem_sleep_path) {
			invalid_fields[kMemSleepPath] = "required_when_suspend_mode_is_mem";
		}
		if (!has_mem_sleep_state) {
			invalid_fields[kMemSleepState] = "required_when_suspend_mode_is_mem";
		}
	} else if (suspend_mode_text != nullptr &&
		   invalid_fields.find(kSuspendMode) == invalid_fields.end()) {
		if (has_mem_sleep_path) {
			invalid_fields[kMemSleepPath] = "only_valid_when_suspend_mode_is_mem";
		}
		if (has_mem_sleep_state) {
			invalid_fields[kMemSleepState] = "only_valid_when_suspend_mode_is_mem";
		}
	}
	if (has_mem_sleep_path && !is_explicit_path(*mem_sleep_path)) {
		invalid_fields[kMemSleepPath] = "must_be_absolute_and_placeholder_free";
	}
	if (has_mem_sleep_state && (mem_sleep_state->find("REPLACE_ME") != std::string::npos ||
				    (*mem_sleep_state != "s2idle" && *mem_sleep_state != "shallow" &&
				     *mem_sleep_state != "deep"))) {
		invalid_fields[kMemSleepState] = "must_be_exactly_s2idle_shallow_or_deep";
	}
	long long suspend_retry_backoff_milliseconds = 0;
	if (suspend_mode != SuspendMode::Disabled) {
		if (suspend_retry_backoff_text == nullptr || suspend_retry_backoff_text->empty()) {
			invalid_fields[kSuspendRetryBackoffMilliseconds] =
				"required_when_suspend_mode_is_enabled";
		} else if (!parse_decimal_integer(*suspend_retry_backoff_text,
					  suspend_retry_backoff_milliseconds) ||
			   suspend_retry_backoff_milliseconds <
				   kMinimumSuspendRetryBackoffMilliseconds ||
			   suspend_retry_backoff_milliseconds >
				   kMaximumSuspendRetryBackoffMilliseconds) {
			invalid_fields[kSuspendRetryBackoffMilliseconds] =
				"must_be_decimal_milliseconds_between_1000_and_300000";
		}
	} else if (suspend_retry_backoff_text != nullptr &&
		   !suspend_retry_backoff_text->empty() &&
		   (!parse_decimal_integer(*suspend_retry_backoff_text,
					   suspend_retry_backoff_milliseconds) ||
		    suspend_retry_backoff_milliseconds <
			    kMinimumSuspendRetryBackoffMilliseconds ||
		    suspend_retry_backoff_milliseconds >
			    kMaximumSuspendRetryBackoffMilliseconds)) {
		invalid_fields[kSuspendRetryBackoffMilliseconds] =
			"when_present_must_be_decimal_milliseconds_between_1000_and_300000";
	}

	if (has_presence_path && !is_explicit_path(*presence_path)) {
		invalid_fields[kPresenceInputPath] = "must_be_absolute_and_placeholder_free";
	}
	if (has_presence_name &&
	    (presence_name->find("REPLACE_ME") != std::string::npos ||
	     presence_name->find('\n') != std::string::npos ||
	     presence_name->find('\r') != std::string::npos)) {
		invalid_fields[kPresenceInputName] = "must_be_exact_and_placeholder_free";
	}
	if (backlight_path != nullptr && !is_explicit_path(*backlight_path)) {
		invalid_fields[kBacklightPath] = "must_be_absolute_and_placeholder_free";
	}
	if (backlight_maximum_path != nullptr && !is_explicit_path(*backlight_maximum_path)) {
		invalid_fields[kBacklightMaximumPath] = "must_be_absolute_and_placeholder_free";
	}
	if (mode_socket_path != nullptr && !is_explicit_path(*mode_socket_path)) {
		invalid_fields[kModeSocket] = "must_be_absolute_and_placeholder_free";
	}
	if (touch_result && touch_result.value() && mode_socket_path != nullptr
	    && touch_result.value()->socket_path == *mode_socket_path) {
		invalid_fields["BHP_PANEL_SUPERVISOR_TOUCH_SOCKET"] = "must_be_separate_from_lifecycle_socket";
	}
	if (power_state_path != nullptr && !power_state_path->empty() &&
	    !is_explicit_path(*power_state_path)) {
		invalid_fields[kPowerStatePath] = "must_be_absolute_and_placeholder_free";
	}

	if (!invalid_fields.empty()) {
		return Result<PanelSupervisordConfig>::failure(
			SupervisorError(ErrorCode::DaemonConfigurationInvalid,
					"Failed to start panel_supervisord because explicit "
					"configuration is incomplete or invalid",
					std::move(invalid_fields)));
	}

	const auto evdev_result = has_presence_path
				  ? EvdevPresenceSource::create(*presence_path)
				  : EvdevPresenceSource::create_from_exact_device_name(*presence_name);
	if (!evdev_result) {
		merge_error_context(invalid_fields, evdev_result.error());
	}
	const auto backlight_result =
		SysfsBacklight::create(*backlight_path, *backlight_maximum_path);
	if (!backlight_result) {
		merge_error_context(invalid_fields, backlight_result.error());
	}
	if (suspend_mode != SuspendMode::Disabled) {
		std::optional<MemSleepSelection> mem_sleep_selection;
		if (suspend_mode == SuspendMode::Mem) {
			mem_sleep_selection = MemSleepSelection{*mem_sleep_path, *mem_sleep_state};
		}
		const auto suspend_result =
			SysfsSuspend::create(*power_state_path, std::string(to_string(suspend_mode)),
					     std::move(mem_sleep_selection));
		if (!suspend_result) {
			merge_error_context(invalid_fields, suspend_result.error());
		}
	}
	if (!invalid_fields.empty()) {
		return Result<PanelSupervisordConfig>::failure(
			SupervisorError(ErrorCode::DaemonConfigurationInvalid,
					"Failed to start panel_supervisord because explicit "
					"adapter paths are invalid",
					std::move(invalid_fields)));
	}

	const auto supervisor_config_result = SupervisorConfig::create(
		std::chrono::milliseconds(grace_seconds * 1000),
		BrightnessLevels{day_brightness, night_brightness, grace_day_brightness,
				 grace_night_brightness, off_brightness},
		std::chrono::milliseconds(presentation_timeout_milliseconds));
	if (!supervisor_config_result) {
		return Result<PanelSupervisordConfig>::failure(SupervisorError::wrap(
			ErrorCode::DaemonConfigurationInvalid,
			"Failed to start panel_supervisord because policy configuration is invalid",
			supervisor_config_result.error()));
	}

	auto configuration = PanelSupervisordConfig(
		supervisor_config_result.value(), latitude_degrees, longitude_degrees,
		has_presence_path ? *presence_path : std::string{},
		has_presence_name ? *presence_name : std::string{}, *backlight_path,
		*backlight_maximum_path, *mode_socket_path,
		static_cast<std::uint32_t>(panel_ui_uid),
		suspend_mode, power_state_path == nullptr ? std::string{} : *power_state_path,
		has_mem_sleep_path ? *mem_sleep_path : std::string{},
		has_mem_sleep_state ? *mem_sleep_state : std::string{},
		std::chrono::milliseconds(poll_interval_milliseconds),
		std::chrono::milliseconds(suspend_retry_backoff_milliseconds));
	configuration.touch_configuration_ = touch_result.value();
	return Result<PanelSupervisordConfig>::success(std::move(configuration));
}

PanelSupervisordConfig::PanelSupervisordConfig(
	SupervisorConfig supervisor_config, const double latitude_degrees,
	const double longitude_degrees, std::string presence_input_path,
	std::string presence_input_name, std::string backlight_path,
	std::string backlight_maximum_path,
	std::string mode_socket_path, const std::uint32_t panel_ui_uid,
	const SuspendMode suspend_mode, std::string power_state_path,
	std::string mem_sleep_path, std::string mem_sleep_state,
	const std::chrono::milliseconds poll_interval,
	const std::chrono::milliseconds suspend_retry_backoff)
    : supervisor_config_(std::move(supervisor_config)), latitude_degrees_(latitude_degrees),
      longitude_degrees_(longitude_degrees), presence_input_path_(std::move(presence_input_path)),
	  presence_input_name_(std::move(presence_input_name)),
	  backlight_path_(std::move(backlight_path)),
      backlight_maximum_path_(std::move(backlight_maximum_path)),
	  mode_socket_path_(std::move(mode_socket_path)), panel_ui_uid_(panel_ui_uid),
	  suspend_mode_(suspend_mode),
	  power_state_path_(std::move(power_state_path)),
	  mem_sleep_path_(std::move(mem_sleep_path)),
	  mem_sleep_state_(std::move(mem_sleep_state)), poll_interval_(poll_interval),
	  suspend_retry_backoff_(suspend_retry_backoff)
{
}

const SupervisorConfig &PanelSupervisordConfig::supervisor_config() const noexcept
{
	return supervisor_config_;
}

double PanelSupervisordConfig::latitude_degrees() const noexcept { return latitude_degrees_; }

double PanelSupervisordConfig::longitude_degrees() const noexcept { return longitude_degrees_; }

const std::string &PanelSupervisordConfig::presence_input_path() const noexcept
{
	return presence_input_path_;
}

const std::string &PanelSupervisordConfig::presence_input_name() const noexcept
{
	return presence_input_name_;
}

const std::string &PanelSupervisordConfig::backlight_path() const noexcept
{
	return backlight_path_;
}

const std::string &PanelSupervisordConfig::backlight_maximum_path() const noexcept
{
	return backlight_maximum_path_;
}

const std::string &PanelSupervisordConfig::mode_socket_path() const noexcept
{
	return mode_socket_path_;
}

std::uint32_t PanelSupervisordConfig::panel_ui_uid() const noexcept { return panel_ui_uid_; }

SuspendMode PanelSupervisordConfig::suspend_mode() const noexcept { return suspend_mode_; }

const std::string &PanelSupervisordConfig::power_state_path() const noexcept
{
	return power_state_path_;
}

const std::string &PanelSupervisordConfig::mem_sleep_path() const noexcept
{
	return mem_sleep_path_;
}

const std::string &PanelSupervisordConfig::mem_sleep_state() const noexcept
{
	return mem_sleep_state_;
}

std::chrono::milliseconds PanelSupervisordConfig::poll_interval() const noexcept
{
	return poll_interval_;
}

std::chrono::milliseconds PanelSupervisordConfig::suspend_retry_backoff() const noexcept
{
	return suspend_retry_backoff_;
}

} // namespace house_panel::supervisor
