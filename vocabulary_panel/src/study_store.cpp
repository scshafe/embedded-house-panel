#include "vocabulary_panel/study_store.h"
#include "bhp/durable_store/durable_store.h"
#include <utility>
namespace vocabulary_panel
{
VocabularyPanelResult<std::optional<std::string>> MemoryStudyStore::read()
{
	return VocabularyPanelResult<std::optional<std::string>>::success(payload_);
}
VocabularyPanelResult<bool> MemoryStudyStore::commit(const std::string &payload)
{
	if (fail_next_commit) {
		fail_next_commit = false;
		return VocabularyPanelResult<bool>::failure(
		    {"STUDY.STORAGE_FAILED", "Injected atomic fixture write failure", {}, false});
	}
	payload_ = payload;
	return VocabularyPanelResult<bool>::success(true);
}
struct FileStudyStore::Implementation {
	bhp::durable_store::DurableFile file;
	Implementation(std::string directory, std::size_t maximum_bytes,
		       std::function<void(StudyStoreBoundary)> observe)
	    : file(std::move(directory), {"study.lock", "study-state.json", "study-state.tmp", "STUDY"},
		   maximum_bytes,
		   observe ? std::function<void(bhp::durable_store::CommitBoundary)>(
				 [observe](bhp::durable_store::CommitBoundary boundary) {
					 observe(static_cast<StudyStoreBoundary>(boundary));
				 })
			   : std::function<void(bhp::durable_store::CommitBoundary)>())
	{
	}
};
namespace
{
VocabularyPanelError study_error(const bhp::durable_store::Error &error)
{
	return {error.code, error.message, error.context, false};
}
} // namespace
FileStudyStore::FileStudyStore(std::string directory, std::size_t maximum_bytes,
			       std::function<void(StudyStoreBoundary)> boundary_observer)
    : implementation_(std::make_unique<Implementation>(std::move(directory), maximum_bytes,
							std::move(boundary_observer)))
{
}
FileStudyStore::~FileStudyStore() = default;
VocabularyPanelResult<std::optional<std::string>> FileStudyStore::read()
{
	using Result = VocabularyPanelResult<std::optional<std::string>>;
	auto value = implementation_->file.read();
	if (!value.has_value())
		return Result::failure(study_error(value.error()));
	return Result::success(value.value());
}
VocabularyPanelResult<bool> FileStudyStore::commit(const std::string &payload)
{
	using Result = VocabularyPanelResult<bool>;
	auto committed = implementation_->file.commit(payload);
	if (!committed.has_value())
		return Result::failure(study_error(committed.error()));
	return Result::success(true);
}
} // namespace vocabulary_panel
