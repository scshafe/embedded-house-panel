#include "fixtures/temporary_directory_fixture.hpp"

#include "panel_supervisor/error_code.hpp"
#include "panel_supervisor/evdev_presence_source.hpp"

#include <array>
#include <iostream>
#include <string_view>
#include <utility>

#if defined(__linux__)
#include <linux/input.h>
#endif

namespace house_panel::supervisor {
namespace {

#define TEST_REQUIRE(condition)                                                                    \
	do {                                                                                       \
		if (!(condition)) {                                                                \
			std::cerr << __func__ << ": requirement failed: " #condition << '\n';      \
			return false;                                                              \
		}                                                                                  \
	} while (false)

bool relative_or_placeholder_path_is_rejected()
{
	const auto relative_result = EvdevPresenceSource::create("dev/input/event0");
	TEST_REQUIRE(!relative_result.has_value());
	TEST_REQUIRE(relative_result.error().code() == ErrorCode::EvdevConfigurationInvalid);
	const auto placeholder_result = EvdevPresenceSource::create("/dev/input/REPLACE_ME");
	TEST_REQUIRE(!placeholder_result.has_value());
	TEST_REQUIRE(placeholder_result.error().code() == ErrorCode::EvdevConfigurationInvalid);
	return true;
}

bool exact_device_name_is_validated_without_guessing_a_path()
{
	const auto name_result =
		EvdevPresenceSource::create_from_exact_device_name("verified-front-proximity");
	TEST_REQUIRE(name_result.has_value());
	TEST_REQUIRE(name_result.value().input_device_path().empty());
	TEST_REQUIRE(name_result.value().exact_device_name() == "verified-front-proximity");

	const auto placeholder_result =
		EvdevPresenceSource::create_from_exact_device_name("REPLACE_ME");
	TEST_REQUIRE(!placeholder_result.has_value());
	TEST_REQUIRE(placeholder_result.error().code() ==
		     ErrorCode::EvdevConfigurationInvalid);
	const auto control_result =
		EvdevPresenceSource::create_from_exact_device_name("sensor\nname");
	TEST_REQUIRE(!control_result.has_value());
	return true;
}

bool regular_file_is_never_mistaken_for_absence()
{
	test_fixture::TemporaryDirectory directory;
	directory.write_text("event-test", "not-an-evdev-device");
	auto source_result = EvdevPresenceSource::create(directory.child_path("event-test"));
	TEST_REQUIRE(source_result.has_value());
	auto source = source_result.value();
	const auto result = source.presence_detected();
	TEST_REQUIRE(!result.has_value());
#if defined(__linux__) && defined(SW_FRONT_PROXIMITY)
	TEST_REQUIRE(result.error().code() == ErrorCode::EvdevCapabilityQueryFailed);
#else
	TEST_REQUIRE(result.error().code() == ErrorCode::PlatformUnsupported);
#endif
	return true;
}

bool missing_device_returns_open_or_platform_failure()
{
	test_fixture::TemporaryDirectory directory;
	auto source_result = EvdevPresenceSource::create(directory.child_path("missing-event"));
	TEST_REQUIRE(source_result.has_value());
	auto source = source_result.value();
	const auto result = source.presence_detected();
	TEST_REQUIRE(!result.has_value());
#if defined(__linux__) && defined(SW_FRONT_PROXIMITY)
	TEST_REQUIRE(result.error().code() == ErrorCode::EvdevDeviceOpenFailed);
#else
	TEST_REQUIRE(result.error().code() == ErrorCode::PlatformUnsupported);
#endif
	return true;
}

using TestFunction = bool (*)();

} // namespace
} // namespace house_panel::supervisor

int main()
{
	using namespace house_panel::supervisor;
	const std::array<std::pair<std::string_view, TestFunction>, 4> tests{{
		{"invalid path", relative_or_placeholder_path_is_rejected},
		{"exact name", exact_device_name_is_validated_without_guessing_a_path},
		{"regular file", regular_file_is_never_mistaken_for_absence},
		{"missing device", missing_device_returns_open_or_platform_failure},
	}};
	for (const auto &[name, test] : tests) {
		if (!test()) {
			std::cerr << "FAILED: " << name << '\n';
			return 1;
		}
	}
	std::cout << "evdev_presence_source_test: " << tests.size() << " passed\n";
	return 0;
}
