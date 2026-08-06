#pragma once

#include "panel_supervisor/presence_source.hpp"

#include <string>

namespace house_panel::supervisor {

// @canonical evdev_presence_source -- Linux EV_SW/SW_FRONT_PROXIMITY current-state adapter.
class EvdevPresenceSource final : public PresenceSource {
public:
	/**
	 * Validates and constructs an evdev presence adapter for an explicit path.
	 *
	 * `input_device_path` must be absolute and contain no placeholder marker.
	 * Invalid input returns PANEL_SUPERVISOR.EVDEV_CONFIGURATION_INVALID.
	 */
	[[nodiscard]] static Result<EvdevPresenceSource> create(std::string input_device_path);

	/**
	 * Constructs exact-name discovery against Linux `/dev/input/event*` nodes.
	 *
	 * Each read requires exactly one device whose `EVIOCGNAME` value exactly
	 * matches and which exposes EV_SW/SW_FRONT_PROXIMITY. Zero or multiple matches
	 * are typed failures; discovery never selects the first candidate.
	 */
	[[nodiscard]] static Result<EvdevPresenceSource>
	create_from_exact_device_name(std::string exact_device_name);

	/**
	 * Opens the configured device and queries EV_SW capability and state via ioctl.
	 *
	 * Missing SW_FRONT_PROXIMITY capability, open/ioctl/close failure, or a
	 * non-Linux build returns a specific typed error. Absence is returned only
	 * after a successful state query.
	 */
	[[nodiscard]] Result<bool> presence_detected() override;

	[[nodiscard]] const std::string &input_device_path() const noexcept;
	[[nodiscard]] const std::string &exact_device_name() const noexcept;

private:
	EvdevPresenceSource(std::string input_device_path, std::string exact_device_name);

	std::string input_device_path_;
	std::string exact_device_name_;
};

} // namespace house_panel::supervisor
