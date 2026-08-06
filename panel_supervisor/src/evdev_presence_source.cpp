#include "panel_supervisor/evdev_presence_source.hpp"

#include "panel_supervisor/error_code.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <map>
#include <string>
#include <string_view>
#include <unistd.h>
#include <utility>
#include <vector>

#if defined(__linux__)
#include <dirent.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#endif

namespace house_panel::supervisor {
namespace {

constexpr std::size_t kMaximumExactDeviceNameBytes = 255;

bool is_explicit_input_path(const std::string &path) noexcept
{
	return !path.empty() && path.front() == '/' &&
	       path.find("REPLACE_ME") == std::string::npos &&
	       path.find('\n') == std::string::npos && path.find('\r') == std::string::npos;
}

bool is_explicit_device_name(const std::string &name) noexcept
{
	if (name.empty() || name.size() > kMaximumExactDeviceNameBytes ||
	    name.find("REPLACE_ME") != std::string::npos) {
		return false;
	}
	return std::all_of(name.begin(), name.end(), [](const unsigned char character) {
		return character >= 0x20U && character != 0x7fU;
	});
}

#if defined(__linux__) && defined(SW_FRONT_PROXIMITY)
constexpr std::string_view kInputDirectory = "/dev/input";
constexpr std::size_t kMaximumEventCandidates = 256;
constexpr std::size_t kDeviceNameBufferBytes = kMaximumExactDeviceNameBytes + 1;
constexpr std::size_t kBitsPerWord = sizeof(unsigned long) * CHAR_BIT;
constexpr std::size_t kSwitchWordCount = (SW_MAX + kBitsPerWord) / kBitsPerWord;

SupervisorError evdev_error(const ErrorCode error_code, const std::string &message,
			    const std::string &path, const char *operation,
			    const int error_number, const bool retryable)
{
	return SupervisorError(error_code, message,
			       {{"errno", std::to_string(error_number)},
				{"operation", operation},
				{"path", path}},
			       retryable);
}

bool bit_is_set(const unsigned long *words, const unsigned int bit) noexcept
{
	return (words[bit / kBitsPerWord] & (1UL << (bit % kBitsPerWord))) != 0;
}

bool is_event_candidate_name(const std::string_view name) noexcept
{
	constexpr std::string_view prefix = "event";
	return name.size() > prefix.size() && name.substr(0, prefix.size()) == prefix &&
	       std::all_of(name.begin() + static_cast<std::ptrdiff_t>(prefix.size()), name.end(),
			   [](const unsigned char character) {
				   return character >= '0' && character <= '9';
			   });
}

Result<int> open_input_device(const std::string &path)
{
	int open_flags = O_RDONLY | O_NONBLOCK;
#ifdef O_CLOEXEC
	open_flags |= O_CLOEXEC;
#endif
	const int file_descriptor = ::open(path.c_str(), open_flags);
	if (file_descriptor < 0) {
		return Result<int>::failure(evdev_error(
			ErrorCode::EvdevDeviceOpenFailed,
			"Failed to open an evdev input device for exact state inspection", path,
			"evdev_presence_source.open", errno,
			errno == EINTR || errno == EAGAIN || errno == EBUSY));
	}
	return Result<int>::success(file_descriptor);
}

Result<void> close_input_device(const int file_descriptor, const std::string &path)
{
	if (::close(file_descriptor) != 0) {
		return Result<void>::failure(evdev_error(
			ErrorCode::EvdevDeviceCloseFailed,
			"Failed to close an evdev input device after exact state inspection", path,
			"evdev_presence_source.close", errno, false));
	}
	return Result<void>::success();
}

Result<std::string> query_device_name(const int file_descriptor, const std::string &path)
{
	std::array<char, kDeviceNameBufferBytes> name_buffer{};
	const int query_result =
		::ioctl(file_descriptor, EVIOCGNAME(name_buffer.size()), name_buffer.data());
	if (query_result < 0) {
		return Result<std::string>::failure(evdev_error(
			ErrorCode::EvdevDeviceNameQueryFailed,
			"Failed to query EVIOCGNAME while discovering the presence device", path,
			"evdev_presence_source.query_device_name", errno,
			errno == EINTR || errno == EAGAIN));
	}
	if (query_result == 0 || static_cast<std::size_t>(query_result) >= name_buffer.size()) {
		return Result<std::string>::failure(SupervisorError(
			ErrorCode::EvdevDeviceNameQueryFailed,
			"Failed to use a missing or truncated EVIOCGNAME discovery result",
			{{"maximum_name_bytes", std::to_string(kMaximumExactDeviceNameBytes)},
			 {"observed_name_bytes", std::to_string(query_result)},
			 {"operation", "evdev_presence_source.query_device_name"},
			 {"path", path}}));
	}
	const std::size_t name_length =
		std::char_traits<char>::length(name_buffer.data());
	return Result<std::string>::success(std::string(name_buffer.data(), name_length));
}

Result<bool> query_front_proximity_capability(const int file_descriptor,
					       const std::string &path)
{
	unsigned long capability_words[kSwitchWordCount]{};
	if (::ioctl(file_descriptor, EVIOCGBIT(EV_SW, sizeof(capability_words)),
		    capability_words) < 0) {
		return Result<bool>::failure(evdev_error(
			ErrorCode::EvdevCapabilityQueryFailed,
			"Failed to query EV_SW capabilities from an evdev device", path,
			"evdev_presence_source.query_capabilities", errno,
			errno == EINTR || errno == EAGAIN));
	}
	return Result<bool>::success(bit_is_set(capability_words, SW_FRONT_PROXIMITY));
}

Result<bool> query_front_proximity_state(const int file_descriptor, const std::string &path)
{
	unsigned long state_words[kSwitchWordCount]{};
	if (::ioctl(file_descriptor, EVIOCGSW(sizeof(state_words)), state_words) < 0) {
		return Result<bool>::failure(evdev_error(
			ErrorCode::EvdevStateQueryFailed,
			"Failed to query current EV_SW state from the selected evdev device", path,
			"evdev_presence_source.query_state", errno,
			errno == EINTR || errno == EAGAIN));
	}
	return Result<bool>::success(bit_is_set(state_words, SW_FRONT_PROXIMITY));
}

Result<bool> read_presence_from_explicit_path(const std::string &path)
{
	auto open_result = open_input_device(path);
	if (!open_result) {
		return Result<bool>::failure(open_result.error());
	}
	const int file_descriptor = open_result.value();
	const auto capability_result = query_front_proximity_capability(file_descriptor, path);
	if (!capability_result) {
		(void)::close(file_descriptor);
		return Result<bool>::failure(capability_result.error());
	}
	if (!capability_result.value()) {
		(void)::close(file_descriptor);
		return Result<bool>::failure(SupervisorError(
			ErrorCode::EvdevCapabilityMissing,
			"Failed to read presence because evdev device does not expose SW_FRONT_PROXIMITY",
			{{"event_code", "SW_FRONT_PROXIMITY"},
			 {"event_type", "EV_SW"},
			 {"operation", "evdev_presence_source.validate_capability"},
			 {"path", path}}));
	}
	const auto state_result = query_front_proximity_state(file_descriptor, path);
	if (!state_result) {
		(void)::close(file_descriptor);
		return Result<bool>::failure(state_result.error());
	}
	const auto close_result = close_input_device(file_descriptor, path);
	if (!close_result) {
		return Result<bool>::failure(close_result.error());
	}
	return state_result;
}

Result<std::vector<std::string>> list_event_candidate_paths()
{
	DIR *input_directory = ::opendir(kInputDirectory.data());
	if (input_directory == nullptr) {
		return Result<std::vector<std::string>>::failure(evdev_error(
			ErrorCode::EvdevDiscoveryDirectoryOpenFailed,
			"Failed to open /dev/input for exact-name presence discovery",
			std::string(kInputDirectory), "evdev_presence_source.opendir", errno,
			errno == EINTR || errno == EAGAIN));
	}

	std::vector<std::string> candidate_paths;
	errno = 0;
	while (dirent *entry = ::readdir(input_directory)) {
		const std::string_view entry_name(entry->d_name);
		if (!is_event_candidate_name(entry_name)) {
			continue;
		}
		candidate_paths.emplace_back(std::string(kInputDirectory) + "/" +
					     std::string(entry_name));
		if (candidate_paths.size() > kMaximumEventCandidates) {
			(void)::closedir(input_directory);
			return Result<std::vector<std::string>>::failure(SupervisorError(
				ErrorCode::EvdevDiscoveryDirectoryReadFailed,
				"Failed exact-name presence discovery because event candidate count exceeds its bound",
				{{"maximum_event_candidates",
				  std::to_string(kMaximumEventCandidates)},
				 {"operation", "evdev_presence_source.list_event_candidates"}}));
		}
	}
	const int read_error_number = errno;
	if (::closedir(input_directory) != 0 && read_error_number == 0) {
		return Result<std::vector<std::string>>::failure(evdev_error(
			ErrorCode::EvdevDiscoveryDirectoryReadFailed,
			"Failed to close /dev/input after exact-name presence discovery",
			std::string(kInputDirectory), "evdev_presence_source.closedir", errno,
			false));
	}
	if (read_error_number != 0) {
		return Result<std::vector<std::string>>::failure(evdev_error(
			ErrorCode::EvdevDiscoveryDirectoryReadFailed,
			"Failed while enumerating /dev/input for exact-name presence discovery",
			std::string(kInputDirectory), "evdev_presence_source.readdir",
			read_error_number, read_error_number == EINTR || read_error_number == EAGAIN));
	}
	std::sort(candidate_paths.begin(), candidate_paths.end());
	return Result<std::vector<std::string>>::success(std::move(candidate_paths));
}

Result<bool> discover_and_read_presence(const std::string &exact_device_name)
{
	const auto candidates_result = list_event_candidate_paths();
	if (!candidates_result) {
		return Result<bool>::failure(candidates_result.error());
	}
	std::size_t exact_name_match_count = 0;
	std::size_t capable_match_count = 0;
	bool matched_presence_state = false;
	for (const std::string &candidate_path : candidates_result.value()) {
		auto open_result = open_input_device(candidate_path);
		if (!open_result) {
			return Result<bool>::failure(open_result.error());
		}
		const int file_descriptor = open_result.value();
		const auto name_result = query_device_name(file_descriptor, candidate_path);
		if (!name_result) {
			(void)::close(file_descriptor);
			return Result<bool>::failure(name_result.error());
		}
		if (name_result.value() == exact_device_name) {
			++exact_name_match_count;
			const auto capability_result =
				query_front_proximity_capability(file_descriptor, candidate_path);
			if (!capability_result) {
				(void)::close(file_descriptor);
				return Result<bool>::failure(capability_result.error());
			}
			if (capability_result.value()) {
				++capable_match_count;
				const auto state_result =
					query_front_proximity_state(file_descriptor, candidate_path);
				if (!state_result) {
					(void)::close(file_descriptor);
					return Result<bool>::failure(state_result.error());
				}
				matched_presence_state = state_result.value();
			}
		}
		const auto close_result = close_input_device(file_descriptor, candidate_path);
		if (!close_result) {
			return Result<bool>::failure(close_result.error());
		}
	}

	if (capable_match_count > 1) {
		return Result<bool>::failure(SupervisorError(
			ErrorCode::EvdevDeviceAmbiguous,
			"Failed exact-name presence discovery because multiple capable devices matched",
			{{"matching_device_count", std::to_string(capable_match_count)},
			 {"operation", "evdev_presence_source.discover_by_exact_name"}}));
	}
	if (capable_match_count == 1) {
		return Result<bool>::success(matched_presence_state);
	}
	if (exact_name_match_count > 0) {
		return Result<bool>::failure(SupervisorError(
			ErrorCode::EvdevCapabilityMissing,
			"Failed exact-name presence discovery because matching devices lack SW_FRONT_PROXIMITY",
			{{"event_code", "SW_FRONT_PROXIMITY"},
			 {"event_type", "EV_SW"},
			 {"matching_name_count", std::to_string(exact_name_match_count)},
			 {"operation", "evdev_presence_source.discover_by_exact_name"}}));
	}
	return Result<bool>::failure(SupervisorError(
		ErrorCode::EvdevDeviceNotFound,
		"Failed exact-name presence discovery because no capable device matched",
		{{"operation", "evdev_presence_source.discover_by_exact_name"}}));
}
#endif

} // namespace

Result<EvdevPresenceSource> EvdevPresenceSource::create(std::string input_device_path)
{
	if (!is_explicit_input_path(input_device_path)) {
		return Result<EvdevPresenceSource>::failure(SupervisorError(
			ErrorCode::EvdevConfigurationInvalid,
			"Failed to create evdev presence source because input path is not explicit",
			{{"input_device_path", "must_be_absolute_and_placeholder_free"}}));
	}
	return Result<EvdevPresenceSource>::success(
		EvdevPresenceSource(std::move(input_device_path), {}));
}

Result<EvdevPresenceSource>
EvdevPresenceSource::create_from_exact_device_name(std::string exact_device_name)
{
	if (!is_explicit_device_name(exact_device_name)) {
		return Result<EvdevPresenceSource>::failure(SupervisorError(
			ErrorCode::EvdevConfigurationInvalid,
			"Failed to create evdev presence source because exact device name is invalid",
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
#if defined(__linux__) && defined(SW_FRONT_PROXIMITY)
	if (!exact_device_name_.empty()) {
		return discover_and_read_presence(exact_device_name_);
	}
	return read_presence_from_explicit_path(input_device_path_);
#else
	return Result<bool>::failure(SupervisorError(
		ErrorCode::PlatformUnsupported,
		"Failed to read EV_SW presence because this build has no Linux SW_FRONT_PROXIMITY ABI",
		{{"operation", "evdev_presence_source.presence_detected"},
		 {"selection_mode", exact_device_name_.empty() ? "explicit_path" : "exact_name"}}));
#endif
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
