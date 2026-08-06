#pragma once

namespace vocabulary_panel {

enum class FlashCardStage {
	loading,
	word,
	definition,
	example,
	rating,
	submitting,
	empty,
	offline,
	offline_complete,
	error,
};

const char *flash_card_stage_name(FlashCardStage flash_card_stage);

} // namespace vocabulary_panel
