#include "fixtures/flash_card_fixture.h"
#include "vocabulary_panel/flash_card_session.h"
#include <filesystem>
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>
using namespace vocabulary_panel;
int main()
{
	int failures = 0;
	for (auto boundary :
	     {StudyStoreBoundary::temporary_opened, StudyStoreBoundary::payload_written,
	      StudyStoreBoundary::file_synchronized, StudyStoreBoundary::renamed,
	      StudyStoreBoundary::directory_synchronized}) {
		auto path = std::filesystem::temp_directory_path() /
			    ("bhp-study-crash-" + new_study_uuid());
		{
			FlashCardSession s(std::make_shared<FileStudyStore>(path.string()));
			if (!s.install_session(test_fixture::three_cards(),
					       test_fixture::session_id())
				 .has_value())
				return 1;
			if (!s.apply_intent(StudyIntent::flip, StudyInputSource::touch,
					    s.current_binding())
				 .has_value())
				return 1;
		}
		auto child = fork();
		if (child == 0) {
			bool armed = false;
			auto store = std::make_shared<FileStudyStore>(
			    path.string(), 4194304, [&](StudyStoreBoundary observed) {
				    if (armed && observed == boundary)
					    _exit(90);
			    });
			FlashCardSession s(store);
			if (!s.restore().has_value())
				_exit(91);
			armed = true;
			s.apply_intent(StudyIntent::rate_good, StudyInputSource::touch,
				       s.current_binding());
			_exit(92);
		}
		int status = 0;
		if (child < 0 || waitpid(child, &status, 0) != child || !WIFEXITED(status) ||
		    WEXITSTATUS(status) != 90)
			return 1;
		{
			FlashCardSession s(std::make_shared<FileStudyStore>(path.string()));
			if (!s.restore().has_value()) {
				++failures;
			} else {
				const bool committed =
				    boundary == StudyStoreBoundary::renamed ||
				    boundary == StudyStoreBoundary::directory_synchronized;
				const auto expected = committed ? 4U : 2U;
				if (s.pending_events().size() != expected ||
				    s.snapshot().can_undo != committed ||
				    s.snapshot().can_rate == committed)
					++failures;
			}
		}
		std::filesystem::remove_all(path);
	}
	std::cout << "study_store_crash: five actual process-death boundaries, " << failures
		  << " failures\n";
	return failures ? 1 : 0;
}
