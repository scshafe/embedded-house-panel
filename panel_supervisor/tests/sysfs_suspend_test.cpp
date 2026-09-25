#include "fixtures/temporary_directory_fixture.hpp"

#include "panel_supervisor/error_code.hpp"
#include "panel_supervisor/sysfs_suspend.hpp"
#include "exact_file_writer.hpp"
#include "sysfs_mem_sleep_access.hpp"

#include <array>
#include <fcntl.h>
#include <iostream>
#include <string_view>
#include <unistd.h>
#include <utility>
#include <string>
#include <vector>

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

MemSleepSelection production_mem_sleep(std::string state)
{
	return MemSleepSelection{"/sys/power/mem_sleep", std::move(state)};
}

bool mem_requires_an_exact_mem_sleep_selection()
{
	const auto missing_result = SysfsSuspend::create("/sys/power/state", "mem");
	TEST_REQUIRE(!missing_result.has_value());
	TEST_REQUIRE(missing_result.error().code() == ErrorCode::SuspendConfigurationInvalid);
	TEST_REQUIRE(missing_result.error().context().at("mem_sleep_state") ==
		     "required_when_suspend_mode_is_mem");

	const auto valid_result =
		SysfsSuspend::create("/sys/power/state", "mem", production_mem_sleep("deep"));
	TEST_REQUIRE(valid_result.has_value());
	TEST_REQUIRE(valid_result.value().mem_sleep_selection().has_value());
	TEST_REQUIRE(valid_result.value().mem_sleep_selection()->mem_sleep_state == "deep");

	const auto freeze_result =
		SysfsSuspend::create("/sys/power/state", "freeze", production_mem_sleep("s2idle"));
	TEST_REQUIRE(!freeze_result.has_value());
	TEST_REQUIRE(freeze_result.error().context().at("mem_sleep_state") ==
		     "only_valid_when_suspend_mode_is_mem");

	const auto path_result = SysfsSuspend::create(
		"/sys/power/state", "mem", MemSleepSelection{"/sys/power/mem_sleep2", "deep"});
	TEST_REQUIRE(!path_result.has_value());
	TEST_REQUIRE(path_result.error().context().count("mem_sleep_path") == 1);

	for (const char *invalid_state :
	     {"", "mem", "freeze", "[deep]", "REPLACE_ME_UNMEASURED_FRDM-GATE-SUSPEND"}) {
		const auto state_result = SysfsSuspend::create(
			"/sys/power/state", "mem", production_mem_sleep(invalid_state));
		TEST_REQUIRE(!state_result.has_value());
		TEST_REQUIRE(state_result.error().context().at("mem_sleep_state") ==
			     "must_be_exactly_s2idle_shallow_or_deep");
	}
	return true;
}

bool kernel_mem_sleep_text_is_parsed_exactly()
{
	const auto deep_result = detail::parse_mem_sleep_text("s2idle [deep]\n");
	TEST_REQUIRE(deep_result.has_value());
	TEST_REQUIRE(deep_result.value().selected == "deep");
	TEST_REQUIRE((deep_result.value().available == std::vector<std::string>{"s2idle", "deep"}));

	const auto only_result = detail::parse_mem_sleep_text("[s2idle]\n");
	TEST_REQUIRE(only_result.has_value());
	TEST_REQUIRE(only_result.value().selected == "s2idle");

	const auto three_result = detail::parse_mem_sleep_text("s2idle [shallow] deep");
	TEST_REQUIRE(three_result.has_value());
	TEST_REQUIRE(three_result.value().selected == "shallow");
	TEST_REQUIRE(three_result.value().available.size() == 3);

	for (const char *malformed :
	     {"", "\n", "s2idle deep\n", "[s2idle] [deep]\n", "s2idle [bogus]\n",
	      "s2idle  [deep]\n", " [deep]\n", "[deep] \n", "[deep]\n\n", "[deep] deep\n",
	      "[deep", "deep]"}) {
		const auto result = detail::parse_mem_sleep_text(malformed);
		TEST_REQUIRE(!result.has_value());
		TEST_REQUIRE(result.error().code() == ErrorCode::MemSleepSelectionFailed);
		TEST_REQUIRE(result.error().context().at("failure_stage") == "parse");
	}
	return true;
}

bool already_selected_variant_is_not_rewritten()
{
	test_fixture::TemporaryDirectory directory;
	directory.write_text("mem_sleep", "s2idle [deep]\n");
	TEST_REQUIRE(
		detail::select_mem_sleep_state_at_path(directory.child_path("mem_sleep"), "deep")
			.has_value());
	TEST_REQUIRE(directory.read_text("mem_sleep") == "s2idle [deep]\n");
	return true;
}

bool unlisted_variant_is_refused_without_writing()
{
	test_fixture::TemporaryDirectory directory;
	directory.write_text("mem_sleep", "[s2idle]\n");
	const auto result =
		detail::select_mem_sleep_state_at_path(directory.child_path("mem_sleep"), "deep");
	TEST_REQUIRE(!result.has_value());
	TEST_REQUIRE(result.error().code() == ErrorCode::MemSleepStateUnavailable);
	TEST_REQUIRE(result.error().context().at("available") == "s2idle");
	TEST_REQUIRE(result.error().context().at("selected") == "s2idle");
	TEST_REQUIRE(directory.read_text("mem_sleep") == "[s2idle]\n");
	return true;
}

bool unconfirmed_selection_fails_closed()
{
	// A regular file keeps the written token verbatim, so the reread has no
	// bracketed selection: exactly the case where the kernel did not confirm.
	test_fixture::TemporaryDirectory directory;
	directory.write_text("mem_sleep", "s2idle [deep]\n");
	const auto result =
		detail::select_mem_sleep_state_at_path(directory.child_path("mem_sleep"), "s2idle");
	TEST_REQUIRE(!result.has_value());
	TEST_REQUIRE(result.error().code() == ErrorCode::MemSleepSelectionFailed);
	TEST_REQUIRE(directory.read_text("mem_sleep") == "s2idle");
	return true;
}

bool missing_mem_sleep_node_is_a_typed_failure()
{
	test_fixture::TemporaryDirectory directory;
	const auto result =
		detail::select_mem_sleep_state_at_path(directory.child_path("mem_sleep"), "deep");
	TEST_REQUIRE(!result.has_value());
	TEST_REQUIRE(result.error().code() == ErrorCode::MemSleepSelectionFailed);
	TEST_REQUIRE(result.error().context().at("failure_stage") == "open");
	return true;
}

using TestFunction = bool (*)();

} // namespace
} // namespace house_panel::supervisor

int main()
{
	using namespace house_panel::supervisor;
	const std::array<std::pair<std::string_view, TestFunction>, 11> tests{{
		{"production power path", only_exact_production_power_state_path_is_accepted},
		{"exact suspend writes",
		 configured_tokens_are_written_exactly_by_canonical_writer},
		{"invalid configuration", invalid_path_and_mode_are_rejected_before_writing},
		{"missing state node", missing_power_state_node_returns_typed_write_failure},
		{"symlink protection", final_power_state_symlink_is_not_followed},
		{"mem requires mem_sleep", mem_requires_an_exact_mem_sleep_selection},
		{"mem_sleep parse", kernel_mem_sleep_text_is_parsed_exactly},
		{"mem_sleep already selected", already_selected_variant_is_not_rewritten},
		{"mem_sleep unlisted", unlisted_variant_is_refused_without_writing},
		{"mem_sleep unconfirmed", unconfirmed_selection_fails_closed},
		{"mem_sleep missing", missing_mem_sleep_node_is_a_typed_failure},
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
