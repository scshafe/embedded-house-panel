#include "panel_ui/bounded_unicode_text_policy.hpp"

#include <cstdint>
#include <utility>

namespace house_panel::panel_ui {
namespace {

PanelUiError unicode_error(const PanelUiErrorCode code, std::string message,
			   const std::string &field)
{
	return PanelUiError(code, std::move(message), {{"field", field}});
}

bool is_forbidden_control(const std::uint32_t code_point, const bool allow_line_breaks) noexcept
{
	if (code_point == 0x0A && allow_line_breaks) {
		return false;
	}
	return code_point < 0x20 || (code_point >= 0x7F && code_point <= 0x9F);
}

} // namespace

PanelUiResult<std::unique_ptr<BoundedUnicodeTextPolicy>>
BoundedUnicodeTextPolicy::create(const UnicodeTextPolicyConfiguration configuration)
{
	if (configuration.maximum_utf8_bytes == 0 || configuration.maximum_code_points == 0) {
		return PanelUiResult<std::unique_ptr<BoundedUnicodeTextPolicy>>::failure(
			unicode_error(PanelUiErrorCode::UnicodeConfigurationInvalid,
				      "Unicode byte and code-point budgets must be explicit and consistent",
				      "unicode_configuration"));
	}
	return PanelUiResult<std::unique_ptr<BoundedUnicodeTextPolicy>>::success(
		std::unique_ptr<BoundedUnicodeTextPolicy>(
			new BoundedUnicodeTextPolicy(configuration)));
}

BoundedUnicodeTextPolicy::BoundedUnicodeTextPolicy(
	const UnicodeTextPolicyConfiguration configuration)
	: configuration_(configuration)
{
}

PanelUiResult<std::string>
BoundedUnicodeTextPolicy::validate(const std::string_view utf8_text) const
{
	if (utf8_text.size() > configuration_.maximum_utf8_bytes) {
		return PanelUiResult<std::string>::failure(unicode_error(
			PanelUiErrorCode::UnicodeByteLimitExceeded,
			"UTF-8 text exceeds the configured byte budget", "utf8_text"));
	}

	std::size_t offset = 0;
	std::size_t code_point_count = 0;
	while (offset < utf8_text.size()) {
		const auto lead = static_cast<std::uint8_t>(utf8_text[offset]);
		std::size_t sequence_size = 0;
		std::uint32_t code_point = 0;
		std::uint32_t minimum_code_point = 0;
		if (lead <= 0x7F) {
			sequence_size = 1;
			code_point = lead;
		} else if ((lead & 0xE0U) == 0xC0U) {
			sequence_size = 2;
			code_point = lead & 0x1FU;
			minimum_code_point = 0x80;
		} else if ((lead & 0xF0U) == 0xE0U) {
			sequence_size = 3;
			code_point = lead & 0x0FU;
			minimum_code_point = 0x800;
		} else if ((lead & 0xF8U) == 0xF0U) {
			sequence_size = 4;
			code_point = lead & 0x07U;
			minimum_code_point = 0x10000;
		} else {
			return PanelUiResult<std::string>::failure(unicode_error(
				PanelUiErrorCode::UnicodeInvalidUtf8,
				"Text contains an invalid UTF-8 leading byte", "utf8_text"));
		}
		if (offset + sequence_size > utf8_text.size()) {
			return PanelUiResult<std::string>::failure(unicode_error(
				PanelUiErrorCode::UnicodeInvalidUtf8,
				"Text ends inside a UTF-8 sequence", "utf8_text"));
		}
		for (std::size_t index = 1; index < sequence_size; ++index) {
			const auto continuation =
				static_cast<std::uint8_t>(utf8_text[offset + index]);
			if ((continuation & 0xC0U) != 0x80U) {
				return PanelUiResult<std::string>::failure(unicode_error(
					PanelUiErrorCode::UnicodeInvalidUtf8,
					"Text contains an invalid UTF-8 continuation byte",
					"utf8_text"));
			}
			code_point = (code_point << 6U) | (continuation & 0x3FU);
		}
		if ((sequence_size > 1 && code_point < minimum_code_point) ||
		    code_point > 0x10FFFF ||
		    (code_point >= 0xD800 && code_point <= 0xDFFF)) {
			return PanelUiResult<std::string>::failure(unicode_error(
				PanelUiErrorCode::UnicodeInvalidUtf8,
				"Text contains a non-canonical or invalid Unicode scalar value",
				"utf8_text"));
		}
		if (is_forbidden_control(code_point, configuration_.allow_line_breaks)) {
			return PanelUiResult<std::string>::failure(unicode_error(
				PanelUiErrorCode::UnicodeControlCharacterForbidden,
				"Text contains a forbidden control character", "utf8_text"));
		}
		offset += sequence_size;
		++code_point_count;
		if (code_point_count > configuration_.maximum_code_points) {
			return PanelUiResult<std::string>::failure(unicode_error(
				PanelUiErrorCode::UnicodeCodePointLimitExceeded,
				"Text exceeds the configured code-point budget", "utf8_text"));
		}
	}

	return PanelUiResult<std::string>::success(std::string(utf8_text));
}

} // namespace house_panel::panel_ui
