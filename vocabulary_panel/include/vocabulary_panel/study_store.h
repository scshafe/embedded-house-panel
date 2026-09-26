#pragma once
#include "vocabulary_panel/vocabulary_panel_result.h"
#include <functional>
#include <memory>
#include <optional>
#include <string>
namespace vocabulary_panel
{
/** @extension-point study_store -- atomic reducer commits, before visible effects. */
class StudyStore
{
      public:
	virtual ~StudyStore() = default;
	virtual VocabularyPanelResult<std::optional<std::string>> read() = 0;
	virtual VocabularyPanelResult<bool> commit(const std::string &payload) = 0;
	virtual bool durable() const = 0;
};
/** Deterministic fixtures only; snapshots explicitly label non-durable storage. */
class MemoryStudyStore final : public StudyStore
{
      public:
	VocabularyPanelResult<std::optional<std::string>> read() override;
	VocabularyPanelResult<bool> commit(const std::string &payload) override;
	bool durable() const override { return false; }
	bool fail_next_commit{false};

      private:
	std::optional<std::string> payload_;
};
enum class StudyStoreBoundary {
	temporary_opened,
	payload_written,
	file_synchronized,
	renamed,
	directory_synchronized
};
/** Owner-only, bounded atomic file with exclusive process lock and fsync ordering. */
class FileStudyStore final : public StudyStore
{
      public:
	explicit FileStudyStore(std::string directory, std::size_t maximum_bytes = 4194304,
				std::function<void(StudyStoreBoundary)> boundary_observer = {});
	~FileStudyStore() override;
	FileStudyStore(const FileStudyStore &) = delete;
	FileStudyStore &operator=(const FileStudyStore &) = delete;
	VocabularyPanelResult<std::optional<std::string>> read() override;
	VocabularyPanelResult<bool> commit(const std::string &payload) override;
	bool durable() const override { return true; }

      private:
	struct Implementation;
	std::unique_ptr<Implementation> implementation_;
};
} // namespace vocabulary_panel
