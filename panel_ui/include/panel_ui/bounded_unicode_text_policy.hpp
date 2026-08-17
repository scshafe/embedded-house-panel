#pragma once

#include "panel_ui/result.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

namespace house_panel::panel_ui {

struct UnicodeTextPolicyConfiguration {
	std::size_t maximum_utf8_bytes;
	std::size_t maximum_code_points;
	bool allow_line_breaks;
};

// @canonical panel_unicode_text_policy -- the shell's sole bounded UTF-8 acceptance policy.
class BoundedUnicodeTextPolicy final {
public:
	[[nodiscard]] static PanelUiResult<std::unique_ptr<BoundedUnicodeTextPolicy>>
	create(UnicodeTextPolicyConfiguration configuration);

	/** Returns the unchanged valid UTF-8 text or a typed content-free rejection. */
	[[nodiscard]] PanelUiResult<std::string> validate(std::string_view utf8_text) const;

private:
	explicit BoundedUnicodeTextPolicy(UnicodeTextPolicyConfiguration configuration);

	UnicodeTextPolicyConfiguration configuration_;
};

} // namespace house_panel::panel_ui
