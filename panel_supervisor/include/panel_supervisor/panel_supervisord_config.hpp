#pragma once

#include "panel_supervisor/result.hpp"
#include "panel_supervisor/supervisor_config.hpp"
#include "panel_supervisor/suspend_mode.hpp"
#include "panel_supervisor/touch_runtime_configuration.hpp"

#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <string>

namespace house_panel::supervisor {

// @canonical panel_supervisord_configuration -- complete, validated Linux runtime configuration.
class PanelSupervisordConfig {
public:
	/**
	 * Loads only documented BHP_PANEL_SUPERVISOR_* environment variables.
	 *
	 * Missing or invalid required fields return
	 * PANEL_SUPERVISOR.DAEMON_CONFIGURATION_INVALID. No filesystem or device is
	 * touched unless this method returns success.
	 */
	[[nodiscard]] static Result<PanelSupervisordConfig> from_environment();

	/**
	 * Validates a complete environment-shaped field map for deterministic tests.
	 *
	 * All numeric values are locale-independent decimal strings. OFF_BRIGHTNESS
	 * must be exactly zero. Enabled suspend modes additionally require
	 * BHP_PANEL_SUPERVISOR_POWER_STATE_PATH; `mem` also requires
	 * BHP_PANEL_SUPERVISOR_MEM_SLEEP_PATH and BHP_PANEL_SUPERVISOR_MEM_SLEEP_STATE,
	 * which every other mode rejects.
	 */
	[[nodiscard]] static Result<PanelSupervisordConfig>
	create(const std::map<std::string, std::string> &configuration_fields);

	[[nodiscard]] const SupervisorConfig &supervisor_config() const noexcept;
	[[nodiscard]] double latitude_degrees() const noexcept;
	[[nodiscard]] double longitude_degrees() const noexcept;
	[[nodiscard]] const std::string &presence_input_path() const noexcept;
	[[nodiscard]] const std::string &presence_input_name() const noexcept;
	[[nodiscard]] const std::string &backlight_path() const noexcept;
	[[nodiscard]] const std::string &backlight_maximum_path() const noexcept;
	[[nodiscard]] const std::string &mode_socket_path() const noexcept;
	[[nodiscard]] std::uint32_t panel_ui_uid() const noexcept;
	[[nodiscard]] SuspendMode suspend_mode() const noexcept;
	[[nodiscard]] const std::string &power_state_path() const noexcept;
	/** Exactly `/sys/power/mem_sleep` for `mem`; empty otherwise. */
	[[nodiscard]] const std::string &mem_sleep_path() const noexcept;
	/** Exactly `s2idle`, `shallow`, or `deep` for `mem`; empty otherwise. */
	[[nodiscard]] const std::string &mem_sleep_state() const noexcept;
	[[nodiscard]] std::chrono::milliseconds poll_interval() const noexcept;
	[[nodiscard]] std::chrono::milliseconds suspend_retry_backoff() const noexcept;
	[[nodiscard]] const std::optional<TouchRuntimeConfiguration> &touch_configuration() const noexcept { return touch_configuration_; }
	/**
	 * The media broker's exact service UID when BHP_PANEL_SUPERVISOR_MEDIA_BROKER_UID is
	 * set. It enables the capture-closure gate's bounded termination path; without it, open
	 * microphone capture refuses every suspend. Never 0 and never the panel UI UID.
	 */
	[[nodiscard]] const std::optional<std::uint32_t> &media_broker_uid() const noexcept { return media_broker_uid_; }
	/**
	 * BHP_PANEL_SUPERVISOR_MEDIA_WINDOW_SOCKET: where the broker reads the lifecycle
	 * window and claims touch observations. Empty when unset; it requires the broker UID.
	 */
	[[nodiscard]] const std::string &media_window_socket() const noexcept { return media_window_socket_; }

private:
	std::optional<TouchRuntimeConfiguration> touch_configuration_;
	std::optional<std::uint32_t> media_broker_uid_;
	std::string media_window_socket_;
	PanelSupervisordConfig(SupervisorConfig supervisor_config, double latitude_degrees,
			       double longitude_degrees, std::string presence_input_path,
			       std::string presence_input_name,
			       std::string backlight_path, std::string backlight_maximum_path,
			       std::string mode_socket_path, std::uint32_t panel_ui_uid,
			       SuspendMode suspend_mode,
			       std::string power_state_path, std::string mem_sleep_path,
			       std::string mem_sleep_state,
			       std::chrono::milliseconds poll_interval,
			       std::chrono::milliseconds suspend_retry_backoff);

	SupervisorConfig supervisor_config_;
	double latitude_degrees_;
	double longitude_degrees_;
	std::string presence_input_path_;
	std::string presence_input_name_;
	std::string backlight_path_;
	std::string backlight_maximum_path_;
	std::string mode_socket_path_;
	std::uint32_t panel_ui_uid_;
	SuspendMode suspend_mode_;
	std::string power_state_path_;
	std::string mem_sleep_path_;
	std::string mem_sleep_state_;
	std::chrono::milliseconds poll_interval_;
	std::chrono::milliseconds suspend_retry_backoff_;
};

} // namespace house_panel::supervisor
