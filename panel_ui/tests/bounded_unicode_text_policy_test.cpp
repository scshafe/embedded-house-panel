#include "panel_ui/bounded_unicode_text_policy.hpp"

#include <iostream>
#include <string>

namespace {

using namespace house_panel::panel_ui;

int test_valid_unicode_is_preserved()
{
	auto policy_result =
		BoundedUnicodeTextPolicy::create({64, 24, true});
	if (!policy_result) {
		return 1;
	}
	const std::string fixture_text = u8"José — 誕生日 🎂";
	const auto result = policy_result.value()->validate(fixture_text);
	return result && result.value() == fixture_text ? 0 : 1;
}

int test_invalid_utf8_is_rejected()
{
	auto policy_result = BoundedUnicodeTextPolicy::create({16, 8, false});
	const std::string invalid_fixture{"\xC0\xAF", 2};
	const auto result = policy_result.value()->validate(invalid_fixture);
	return !result && result.error().code() == PanelUiErrorCode::UnicodeInvalidUtf8 ? 0 : 1;
}

int test_byte_budget_is_enforced()
{
	auto policy_result = BoundedUnicodeTextPolicy::create({4, 4, false});
	const auto result = policy_result.value()->validate("abcde");
	return !result && result.error().code() == PanelUiErrorCode::UnicodeByteLimitExceeded ? 0
											      : 1;
}

int test_code_point_budget_is_enforced()
{
	auto policy_result = BoundedUnicodeTextPolicy::create({16, 2, false});
	const auto result = policy_result.value()->validate("abc");
	return !result && result.error().code() == PanelUiErrorCode::UnicodeCodePointLimitExceeded
		       ? 0
		       : 1;
}

int test_control_characters_are_rejected()
{
	auto policy_result = BoundedUnicodeTextPolicy::create({16, 8, false});
	const std::string control_fixture{"a\nb", 3};
	const auto result = policy_result.value()->validate(control_fixture);
	return !result &&
		       result.error().code() == PanelUiErrorCode::UnicodeControlCharacterForbidden
		       ? 0
		       : 1;
}

} // namespace

int main()
{
	const struct {
		const char *name;
		int (*run)();
	} tests[] = {
		{"test_valid_unicode_is_preserved", test_valid_unicode_is_preserved},
		{"test_invalid_utf8_is_rejected", test_invalid_utf8_is_rejected},
		{"test_byte_budget_is_enforced", test_byte_budget_is_enforced},
		{"test_code_point_budget_is_enforced", test_code_point_budget_is_enforced},
		{"test_control_characters_are_rejected", test_control_characters_are_rejected},
	};
	for (const auto &test : tests) {
		if (test.run() != 0) {
			std::cerr << test.name << " failed\n";
			return 1;
		}
	}
	return 0;
}
