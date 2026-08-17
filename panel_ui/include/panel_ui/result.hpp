#pragma once

#include "panel_ui/panel_ui_error.hpp"

#include <utility>
#include <variant>

namespace house_panel::panel_ui {

template <typename T> class PanelUiResult final {
public:
	[[nodiscard]] static PanelUiResult success(T value)
	{
		return PanelUiResult(std::move(value));
	}

	[[nodiscard]] static PanelUiResult failure(PanelUiError error)
	{
		return PanelUiResult(std::move(error));
	}

	[[nodiscard]] explicit operator bool() const noexcept
	{
		return std::holds_alternative<T>(value_);
	}

	[[nodiscard]] const T &value() const & { return std::get<T>(value_); }
	[[nodiscard]] T &value() & { return std::get<T>(value_); }
	[[nodiscard]] T &&value() && { return std::get<T>(std::move(value_)); }
	[[nodiscard]] const PanelUiError &error() const &
	{
		return std::get<PanelUiError>(value_);
	}

private:
	explicit PanelUiResult(T value) : value_(std::move(value)) {}
	explicit PanelUiResult(PanelUiError error) : value_(std::move(error)) {}

	std::variant<T, PanelUiError> value_;
};

} // namespace house_panel::panel_ui
