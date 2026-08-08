#include "panel_supervisor/evdev_presence_source.hpp"

#include "panel_supervisor/error_code.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <fcntl.h>
#include <string>
#include <utility>
#include <unistd.h>

// Test-link replacement for the canonical evdev adapter. The daemon smoke build links this file
// instead of src/evdev_presence_source.cpp so a one-byte regular-file fixture can drive real
// process cycles without claiming Linux input-device or target-hardware evidence.

namespace house_panel::supervisor {
namespace {

bool is_explicit_fixture_path(const std::string &path) noexcept
{
	return !path.empty() && path.front() == '/' &&
	       path.find("REPLACE_ME") == std::string::npos &&
	       path.find('\n') == std::string::npos && path.find('\r') == std::string::npos;
}

bool is_explicit_fixture_name(const std::string &name) noexcept
{
	return !name.empty() && name.size() <= 255 &&
	       name.find("REPLACE_ME") == std::string::npos &&
	       std::all_of(name.begin(), name.end(), [](const unsigned char character) {
		       return character >= 0x20U && character != 0x7fU;
	       });
}

SupervisorError fixture_error(const ErrorCode code, const std::string &message,
			      const std::string &path, const char *operation,
			      const int error_number = 0)
{
	return SupervisorError(code, message,
			       {{"errno", std::to_string(error_number)},
				{"fixture_kind", "fake_evdev_presence_state"},
				{"operation", operation},
				{"path", path}},
			       error_number == EINTR || error_number == EAGAIN);
}

} // namespace

Result<EvdevPresenceSource> EvdevPresenceSource::create(std::string input_device_path)
{
	if (!is_explicit_fixture_path(input_device_path)) {
		return Result<EvdevPresenceSource>::failure(SupervisorError(
			ErrorCode::EvdevConfigurationInvalid,
			"Failed to create fake evdev presence source because its fixture path is not explicit",
			{{"input_device_path", "must_be_absolute_and_placeholder_free"}}));
	}
	return Result<EvdevPresenceSource>::success(
		EvdevPresenceSource(std::move(input_device_path), {}));
}

Result<EvdevPresenceSource>
EvdevPresenceSource::create_from_exact_device_name(std::string exact_device_name)
{
	if (!is_explicit_fixture_name(exact_device_name)) {
		return Result<EvdevPresenceSource>::failure(SupervisorError(
			ErrorCode::EvdevConfigurationInvalid,
			"Failed to create fake evdev presence source because its exact fixture name is invalid",
			{{"exact_device_name",
			  "must_be_1_to_255_bytes_control_character_free_and_placeholder_free"}}));
	}
	return Result<EvdevPresenceSource>::success(
		EvdevPresenceSource({}, std::move(exact_device_name)));
}

EvdevPresenceSource::EvdevPresenceSource(std::string input_device_path,
					 std::string exact_device_name)
	: input_device_path_(std::move(input_device_path)),
	  exact_device_name_(std::move(exact_device_name))
{
}

Result<bool> EvdevPresenceSource::presence_detected()
{
	if (input_device_path_.empty()) {
		return Result<bool>::failure(SupervisorError(
			ErrorCode::EvdevDeviceNotFound,
			"Failed to read fake evdev presence because the daemon smoke fixture requires an explicit path selector",
			{{"fixture_kind", "fake_evdev_presence_state"},
			 {"operation", "fake_evdev_presence_source.presence_detected"},
			 {"selection_mode", "exact_name_unsupported"}}));
	}

	int open_flags = O_RDONLY;
#ifdef O_CLOEXEC
	open_flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
	open_flags |= O_NOFOLLOW;
#endif
	const int descriptor = ::open(input_device_path_.c_str(), open_flags);
	if (descriptor < 0) {
		return Result<bool>::failure(fixture_error(
			ErrorCode::EvdevDeviceOpenFailed,
			"Failed to open the fake evdev presence-state fixture",
			input_device_path_, "fake_evdev_presence_source.open", errno));
	}

	std::array<char, 2> state_bytes{};
	ssize_t read_result = 0;
	do {
		read_result = ::read(descriptor, state_bytes.data(), state_bytes.size());
	} while (read_result < 0 && errno == EINTR);
	const int read_error_number = read_result < 0 ? errno : 0;
	if (::close(descriptor) != 0) {
		return Result<bool>::failure(fixture_error(
			ErrorCode::EvdevDeviceCloseFailed,
			"Failed to close the fake evdev presence-state fixture",
			input_device_path_, "fake_evdev_presence_source.close", errno));
	}
	if (read_result < 0) {
		return Result<bool>::failure(fixture_error(
			ErrorCode::EvdevStateQueryFailed,
			"Failed to read the fake evdev presence-state fixture",
			input_device_path_, "fake_evdev_presence_source.read",
			read_error_number));
	}
	if (read_result != 1 || (state_bytes[0] != '0' && state_bytes[0] != '1')) {
		return Result<bool>::failure(fixture_error(
			ErrorCode::EvdevStateQueryFailed,
			"Failed to parse fake evdev presence state; expected exactly one byte containing 0 or 1",
			input_device_path_, "fake_evdev_presence_source.parse"));
	}
	return Result<bool>::success(state_bytes[0] == '1');
}

const std::string &EvdevPresenceSource::input_device_path() const noexcept
{
	return input_device_path_;
}

const std::string &EvdevPresenceSource::exact_device_name() const noexcept
{
	return exact_device_name_;
}

} // namespace house_panel::supervisor
