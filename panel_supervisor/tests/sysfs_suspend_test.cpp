#include "fixtures/temporary_directory_fixture.hpp"

#include "panel_supervisor/error_code.hpp"
#include "panel_supervisor/sysfs_suspend.hpp"
#include "exact_file_writer.hpp"

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

bool only_exact_production_power_state_path_is_accepted()
{
	const auto valid_result = SysfsSuspend::create("/sys/power/state", "freeze");
	TEST_REQUIRE(valid_result.has_value());
	TEST_REQUIRE(valid_result.value().power_state_path() == "/sys/power/state");
	const auto alternate_result =
		SysfsSuspend::create("/sys/devices/power/state", "freeze");
	TEST_REQUIRE(!alternate_result.has_value());
	TEST_REQUIRE(alternate_result.error().code() ==
		     ErrorCode::SuspendConfigurationInvalid);
	return true;
}

bool configured_tokens_are_written_exactly_by_canonical_writer()
{
	test_fixture::TemporaryDirectory directory;
	directory.write_text("state", "initial");
	TEST_REQUIRE(detail::write_exact_file(directory.child_path("state"), "freeze",
					      ErrorCode::PowerStateWriteFailed,
					      "sysfs_suspend_test.freeze")
			     .has_value());
	TEST_REQUIRE(directory.read_text("state") == "freeze");

	TEST_REQUIRE(detail::write_exact_file(directory.child_path("state"), "mem",
					      ErrorCode::PowerStateWriteFailed,
					      "sysfs_suspend_test.mem")
			     .has_value());
	TEST_REQUIRE(directory.read_text("state") == "mem");
	return true;
}

bool invalid_path_and_mode_are_rejected_before_writing()
{
	const auto relative_result = SysfsSuspend::create("sys/power/state", "freeze");
	TEST_REQUIRE(!relative_result.has_value());
	TEST_REQUIRE(relative_result.error().code() == ErrorCode::SuspendConfigurationInvalid);

	const auto mode_result = SysfsSuspend::create("/sys/power/state", "disk");
	TEST_REQUIRE(!mode_result.has_value());
	TEST_REQUIRE(mode_result.error().code() == ErrorCode::SuspendConfigurationInvalid);
	TEST_REQUIRE(mode_result.error().context().count("suspend_mode") == 1);
	return true;
}

bool missing_power_state_node_returns_typed_write_failure()
{
	test_fixture::TemporaryDirectory directory;
	const auto result = detail::write_exact_file(
		directory.child_path("state"), "freeze", ErrorCode::PowerStateWriteFailed,
		"sysfs_suspend_test.missing_state");
	TEST_REQUIRE(!result.has_value());
	TEST_REQUIRE(result.error().code() == ErrorCode::PowerStateWriteFailed);
	TEST_REQUIRE(result.error().context().at("failure_stage") == "open");
	return true;
}

bool final_power_state_symlink_is_not_followed()
{
	test_fixture::TemporaryDirectory directory;
	directory.write_text("real_state", "protected");
	TEST_REQUIRE(::symlink(directory.child_path("real_state").c_str(),
			       directory.child_path("state").c_str()) == 0);
	const auto result = detail::write_exact_file(
		directory.child_path("state"), "mem", ErrorCode::PowerStateWriteFailed,
		"sysfs_suspend_test.symlink_state");
#if defined(O_NOFOLLOW)
	TEST_REQUIRE(!result.has_value());
	TEST_REQUIRE(result.error().code() == ErrorCode::PowerStateWriteFailed);
	TEST_REQUIRE(directory.read_text("real_state") == "protected");
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
	const std::array<std::pair<std::string_view, TestFunction>, 5> tests{{
		{"production power path", only_exact_production_power_state_path_is_accepted},
		{"exact suspend writes",
		 configured_tokens_are_written_exactly_by_canonical_writer},
		{"invalid configuration", invalid_path_and_mode_are_rejected_before_writing},
		{"missing state node", missing_power_state_node_returns_typed_write_failure},
		{"symlink protection", final_power_state_symlink_is_not_followed},
	}};
	for (const auto &[name, test] : tests) {
		if (!test()) {
			std::cerr << "FAILED: " << name << '\n';
			return 1;
		}
	}
	std::cout << "sysfs_suspend_test: " << tests.size() << " passed\n";
	return 0;
}
