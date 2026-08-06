#include "fixtures/temporary_directory_fixture.hpp"

#include "panel_supervisor/error_code.hpp"
#include "panel_supervisor/sysfs_backlight.hpp"
#include "sysfs_backlight_file_access.hpp"

#include <array>
#include <fcntl.h>
#include <iostream>
#include <string_view>
#include <unistd.h>
#include <utility>

namespace house_panel::supervisor {
namespace {

#define TEST_REQUIRE(condition)                                                                    \
	do {                                                                                       \
		if (!(condition)) {                                                                \
			std::cerr << __func__ << ": requirement failed: " #condition << '\n';      \
			return false;                                                              \
		}                                                                                  \
	} while (false)

Result<void> set_test_brightness(const test_fixture::TemporaryDirectory &directory,
				const int brightness_percent)
{
	return detail::set_backlight_brightness_percent_at_paths(
		directory.child_path("brightness"), directory.child_path("max_brightness"),
		brightness_percent);
}

bool production_paths_require_one_exact_backlight_device_component()
{
	const auto valid_result = SysfsBacklight::create(
		"/sys/class/backlight/panel-0/brightness",
		"/sys/class/backlight/panel-0/max_brightness");
	TEST_REQUIRE(valid_result.has_value());
	TEST_REQUIRE(valid_result.value().brightness_path() ==
		     "/sys/class/backlight/panel-0/brightness");

	const auto nested_result = SysfsBacklight::create(
		"/sys/class/backlight/group/panel/brightness",
		"/sys/class/backlight/group/panel/max_brightness");
	TEST_REQUIRE(!nested_result.has_value());
	TEST_REQUIRE(nested_result.error().code() == ErrorCode::BacklightConfigurationInvalid);
	const auto mismatched_result = SysfsBacklight::create(
		"/sys/class/backlight/panel-0/brightness",
		"/sys/class/backlight/panel-1/max_brightness");
	TEST_REQUIRE(!mismatched_result.has_value());
	const auto outside_result = SysfsBacklight::create(
		"/tmp/panel/brightness", "/tmp/panel/max_brightness");
	TEST_REQUIRE(!outside_result.has_value());
	return true;
}

bool logical_percent_is_rounded_and_maximum_is_reread()
{
	test_fixture::TemporaryDirectory directory;
	directory.write_text("brightness", "old");
	directory.write_text("max_brightness", "255\n");
	TEST_REQUIRE(set_test_brightness(directory, 50).has_value());
	TEST_REQUIRE(directory.read_text("brightness") == "128");
	directory.write_text("max_brightness", "10");
	TEST_REQUIRE(set_test_brightness(directory, 50).has_value());
	TEST_REQUIRE(directory.read_text("brightness") == "5");
	return true;
}

bool boundary_percentages_map_exactly_without_newline()
{
	test_fixture::TemporaryDirectory directory;
	directory.write_text("brightness", "old");
	directory.write_text("max_brightness", "4095\n");
	TEST_REQUIRE(set_test_brightness(directory, 0).has_value());
	TEST_REQUIRE(directory.read_text("brightness") == "0");
	TEST_REQUIRE(set_test_brightness(directory, 100).has_value());
	TEST_REQUIRE(directory.read_text("brightness") == "4095");
	return true;
}

bool malformed_or_zero_maximum_never_changes_brightness()
{
	test_fixture::TemporaryDirectory directory;
	directory.write_text("brightness", "unchanged");
	directory.write_text("max_brightness", "not-a-number");
	const auto malformed_result = set_test_brightness(directory, 20);
	TEST_REQUIRE(!malformed_result.has_value());
	TEST_REQUIRE(malformed_result.error().code() == ErrorCode::BacklightMaximumInvalid);
	TEST_REQUIRE(directory.read_text("brightness") == "unchanged");

	directory.write_text("max_brightness", "0\n");
	const auto zero_result = set_test_brightness(directory, 20);
	TEST_REQUIRE(!zero_result.has_value());
	TEST_REQUIRE(zero_result.error().code() == ErrorCode::BacklightMaximumInvalid);
	TEST_REQUIRE(directory.read_text("brightness") == "unchanged");
	return true;
}

bool invalid_percent_and_missing_nodes_return_specific_errors()
{
	test_fixture::TemporaryDirectory directory;
	directory.write_text("brightness", "unchanged");
	directory.write_text("max_brightness", "100");
	const auto invalid_percent_result = set_test_brightness(directory, 101);
	TEST_REQUIRE(!invalid_percent_result.has_value());
	TEST_REQUIRE(invalid_percent_result.error().code() ==
		     ErrorCode::BacklightDeviceWriteFailed);
	TEST_REQUIRE(directory.read_text("brightness") == "unchanged");

	TEST_REQUIRE(::unlink(directory.child_path("max_brightness").c_str()) == 0);
	const auto missing_maximum_result = set_test_brightness(directory, 50);
	TEST_REQUIRE(!missing_maximum_result.has_value());
	TEST_REQUIRE(missing_maximum_result.error().code() ==
		     ErrorCode::BacklightMaximumReadFailed);
	return true;
}

bool final_brightness_symlink_is_not_followed()
{
	test_fixture::TemporaryDirectory directory;
	directory.write_text("real_brightness", "protected");
	directory.write_text("max_brightness", "100");
	TEST_REQUIRE(::symlink(directory.child_path("real_brightness").c_str(),
			       directory.child_path("brightness").c_str()) == 0);
	const auto result = set_test_brightness(directory, 75);
#if defined(O_NOFOLLOW)
	TEST_REQUIRE(!result.has_value());
	TEST_REQUIRE(result.error().code() == ErrorCode::BacklightDeviceWriteFailed);
	TEST_REQUIRE(directory.read_text("real_brightness") == "protected");
#else
	TEST_REQUIRE(result.has_value());
#endif
	return true;
}

using TestFunction = bool (*)();

} // namespace
} // namespace house_panel::supervisor

int main()
{
	using namespace house_panel::supervisor;
	const std::array<std::pair<std::string_view, TestFunction>, 6> tests{{
		{"production path shape",
		 production_paths_require_one_exact_backlight_device_component},
		{"logical mapping", logical_percent_is_rounded_and_maximum_is_reread},
		{"boundary mapping", boundary_percentages_map_exactly_without_newline},
		{"invalid maximum", malformed_or_zero_maximum_never_changes_brightness},
		{"invalid inputs", invalid_percent_and_missing_nodes_return_specific_errors},
		{"symlink protection", final_brightness_symlink_is_not_followed},
	}};
	for (const auto &[name, test] : tests) {
		if (!test()) {
			std::cerr << "FAILED: " << name << '\n';
			return 1;
		}
	}
	std::cout << "sysfs_backlight_test: " << tests.size() << " passed\n";
	return 0;
}
