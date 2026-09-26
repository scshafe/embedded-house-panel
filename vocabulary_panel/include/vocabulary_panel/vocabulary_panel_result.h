#pragma once

#include "vocabulary_panel/vocabulary_panel_error.h"

#include <stdexcept>
#include <utility>
#include <variant>

namespace vocabulary_panel
{

template <typename Value> class VocabularyPanelResult final
{
      public:
	static VocabularyPanelResult success(Value value)
	{
		return VocabularyPanelResult(std::move(value));
	}

	static VocabularyPanelResult failure(VocabularyPanelError error)
	{
		return VocabularyPanelResult(std::move(error));
	}

	bool has_value() const { return std::holds_alternative<Value>(storage_); }

	const Value &value() const
	{
		if (!has_value())
			throw std::logic_error("VOCABULARY_PANEL.RESULT_VALUE_UNAVAILABLE");
		return std::get<Value>(storage_);
	}

	const VocabularyPanelError &error() const
	{
		if (has_value())
			throw std::logic_error("VOCABULARY_PANEL.RESULT_ERROR_UNAVAILABLE");
		return std::get<VocabularyPanelError>(storage_);
	}

      private:
	explicit VocabularyPanelResult(Value value) : storage_(std::move(value)) {}
	explicit VocabularyPanelResult(VocabularyPanelError error) : storage_(std::move(error)) {}

	std::variant<Value, VocabularyPanelError> storage_;
};

} // namespace vocabulary_panel
