#include "panel_supervisor/sysfs_backlight.hpp"

#include "exact_file_writer.hpp"
#include "panel_supervisor/error_code.hpp"
#include "sysfs_backlight_file_access.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <map>
#include <string>
#include <string_view>
#include <unistd.h>
#include <utility>

namespace house_panel::supervisor {
namespace {

constexpr std::size_t kMaximumBrightnessTextBytes = 64;

bool is_valid_backlight_device_component(const std::string_view component) noexcept
{
	return !component.empty() && component != "." && component != ".." &&
	       component.find('/') == std::string_view::npos &&
	       std::all_of(component.begin(), component.end(), [](const unsigned char character) {
		       return character >= 0x20U && character != 0x7fU;
	       });
}

bool are_exact_production_backlight_paths(const std::string &brightness_path,
					  const std::string &maximum_brightness_path)
{
	constexpr std::string_view prefix = "/sys/class/backlight/";
	constexpr std::string_view brightness_suffix = "/brightness";
	if (brightness_path.size() <= prefix.size() + brightness_suffix.size() ||
	    brightness_path.compare(0, prefix.size(), prefix) != 0 ||
	    brightness_path.compare(brightness_path.size() - brightness_suffix.size(),
				    brightness_suffix.size(), brightness_suffix) != 0) {
		return false;
	}
	const std::size_t device_length =
		brightness_path.size() - prefix.size() - brightness_suffix.size();
	const std::string_view device_name(brightness_path.data() + prefix.size(),
					   device_length);
	if (!is_valid_backlight_device_component(device_name)) {
		return false;
	}
	return maximum_brightness_path ==
	       std::string(prefix) + std::string(device_name) + "/max_brightness";
}

SupervisorError maximum_read_error(const std::string &path, const char *failure_stage,
				   const int error_number)
{
	return SupervisorError(ErrorCode::BacklightMaximumReadFailed,
			       "Failed to read the configured backlight maximum brightness node",
			       {{"errno", std::to_string(error_number)},
				{"failure_stage", failure_stage},
				{"operation", "sysfs_backlight.read_maximum_brightness"},
				{"path", path}},
			       error_number == EINTR || error_number == EAGAIN ||
				       error_number == EBUSY);
}

Result<std::uint64_t> read_maximum_brightness(const std::string &path)
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
		return Result<std::uint64_t>::failure(maximum_read_error(path, "open", errno));
	}

	std::array<char, kMaximumBrightnessTextBytes + 1> buffer{};
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
			return Result<std::uint64_t>::failure(
				maximum_read_error(path, "read", read_error_number));
		}
		if (read_result == 0) {
			break;
		}
		bytes_read += static_cast<std::size_t>(read_result);
	}

	if (::close(file_descriptor) != 0) {
		return Result<std::uint64_t>::failure(maximum_read_error(path, "close", errno));
	}
	if (bytes_read == 0 || bytes_read > kMaximumBrightnessTextBytes) {
		return Result<std::uint64_t>::failure(SupervisorError(
			ErrorCode::BacklightMaximumInvalid,
			"Failed to parse backlight maximum brightness because its text length is "
			"invalid",
			{{"maximum_text_bytes", std::to_string(kMaximumBrightnessTextBytes)},
			 {"observed_text_bytes", std::to_string(bytes_read)},
			 {"operation", "sysfs_backlight.parse_maximum_brightness"},
			 {"path", path}}));
	}

	std::string_view maximum_text(buffer.data(), bytes_read);
	while (!maximum_text.empty() &&
	       std::isspace(static_cast<unsigned char>(maximum_text.front())) != 0) {
		maximum_text.remove_prefix(1);
	}
	while (!maximum_text.empty() &&
	       std::isspace(static_cast<unsigned char>(maximum_text.back())) != 0) {
		maximum_text.remove_suffix(1);
	}
	std::uint64_t maximum_brightness = 0;
	const auto parse_result = std::from_chars(
		maximum_text.data(), maximum_text.data() + maximum_text.size(), maximum_brightness);
	if (maximum_text.empty() || parse_result.ec != std::errc{} ||
	    parse_result.ptr != maximum_text.data() + maximum_text.size() ||
	    maximum_brightness == 0) {
		return Result<std::uint64_t>::failure(SupervisorError(
			ErrorCode::BacklightMaximumInvalid,
			"Failed to parse a positive decimal backlight maximum brightness",
			{{"operation", "sysfs_backlight.parse_maximum_brightness"},
			 {"path", path}}));
	}
	return Result<std::uint64_t>::success(maximum_brightness);
}

} // namespace

Result<SysfsBacklight> SysfsBacklight::create(std::string brightness_path,
					      std::string maximum_brightness_path)
{
	if (!are_exact_production_backlight_paths(brightness_path, maximum_brightness_path)) {
		return Result<SysfsBacklight>::failure(SupervisorError(
			ErrorCode::BacklightConfigurationInvalid,
			"Failed to create sysfs backlight adapter because paths are outside the exact production shape",
			{{"backlight_paths",
			  "must_be_exact_same_device_/sys/class/backlight/<single-component>/{brightness,max_brightness}"}}));
	}
	return Result<SysfsBacklight>::success(
		SysfsBacklight(std::move(brightness_path), std::move(maximum_brightness_path)));
}

SysfsBacklight::SysfsBacklight(std::string brightness_path, std::string maximum_brightness_path)
    : brightness_path_(std::move(brightness_path)),
      maximum_brightness_path_(std::move(maximum_brightness_path))
{
}

Result<void> SysfsBacklight::set_brightness_percent(const int brightness_percent)
{
	return detail::set_backlight_brightness_percent_at_paths(
		brightness_path_, maximum_brightness_path_, brightness_percent);
}

namespace detail {

Result<void> set_backlight_brightness_percent_at_paths(
	const std::string &brightness_path, const std::string &maximum_brightness_path,
	const int brightness_percent)
{
	if (brightness_percent < 0 || brightness_percent > 100) {
		return Result<void>::failure(
			SupervisorError(ErrorCode::BacklightDeviceWriteFailed,
					"Failed to apply backlight brightness because logical "
					"percent is outside 0..100",
					{{"brightness_percent", std::to_string(brightness_percent)},
					 {"operation", "sysfs_backlight.set_brightness_percent"}}));
	}
	const auto maximum_result = read_maximum_brightness(maximum_brightness_path);
	if (!maximum_result) {
		return Result<void>::failure(maximum_result.error());
	}
	const std::uint64_t maximum = maximum_result.value();
	const std::uint64_t percent = static_cast<std::uint64_t>(brightness_percent);
	const std::uint64_t mapped_brightness =
		(maximum / 100U) * percent + (((maximum % 100U) * percent + 50U) / 100U);
	return write_exact_file(brightness_path, std::to_string(mapped_brightness),
				ErrorCode::BacklightDeviceWriteFailed,
				"sysfs_backlight.set_brightness_percent");
}

} // namespace detail

const std::string &SysfsBacklight::brightness_path() const noexcept { return brightness_path_; }

const std::string &SysfsBacklight::maximum_brightness_path() const noexcept
{
	return maximum_brightness_path_;
}

} // namespace house_panel::supervisor
