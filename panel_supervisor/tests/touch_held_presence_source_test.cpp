#include "panel_supervisor/error_code.hpp"
#include "panel_supervisor/touch_held_presence_source.hpp"

#include <array>
#include <iostream>
#include <string_view>
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

using Clock = TouchHeldPresenceSource::Clock;

class ScriptedPresence final : public PresenceSource {
public:
	Result<bool> presence_detected() override
	{
		if (failing) return Result<bool>::failure({ErrorCode::AdapterUnavailable, "presence read failed", {}});
		return Result<bool>::success(present);
	}
	bool present{false};
	bool failing{false};
};

struct Harness {
	ScriptedPresence inner;
	Clock::time_point now{Clock::time_point{} + std::chrono::hours(1)};
	std::optional<Clock::time_point> touched;
	TouchHeldPresenceSource source{inner, [this] { return touched; }, std::chrono::seconds(300),
				       [this] { return now; }};
};

bool no_touch_keeps_the_inner_answer()
{
	Harness harness;
	TEST_REQUIRE(harness.source.presence_detected().value() == false);
	harness.inner.present = true;
	TEST_REQUIRE(harness.source.presence_detected().value() == true);
	return true;
}

bool a_touch_holds_presence_for_the_configured_time()
{
	Harness harness;
	harness.touched = harness.now;
	harness.now += std::chrono::seconds(299);
	TEST_REQUIRE(harness.source.presence_detected().value() == true);
	harness.now += std::chrono::seconds(1);
	TEST_REQUIRE(harness.source.presence_detected().value() == false);
	return true;
}

bool an_inner_failure_is_never_hidden_by_a_touch()
{
	Harness harness;
	harness.touched = harness.now;
	harness.inner.failing = true;
	const auto result = harness.source.presence_detected();
	TEST_REQUIRE(!result.has_value());
	TEST_REQUIRE(result.error().code() == ErrorCode::AdapterUnavailable);
	return true;
}

using TestFunction = bool (*)();

} // namespace
} // namespace house_panel::supervisor

int main()
{
	using namespace house_panel::supervisor;
	const std::array<std::pair<std::string_view, TestFunction>, 3> tests{{
		{"no touch", no_touch_keeps_the_inner_answer},
		{"touch hold", a_touch_holds_presence_for_the_configured_time},
		{"inner failure", an_inner_failure_is_never_hidden_by_a_touch},
	}};
	bool passed = true;
	for (const auto &[name, test] : tests) {
		const bool ok = test();
		std::cout << (ok ? "PASS " : "FAIL ") << name << '\n';
		passed = passed && ok;
	}
	return passed ? 0 : 1;
}
