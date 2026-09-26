#include "bhp/structured_logger.h"
#include "vocabulary_panel/flash_card_session.h"
#include "vocabulary_panel/http_lexinet_client.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <unistd.h>
using namespace vocabulary_panel;
namespace
{
bool synchronize(FlashCardSession &session, HttpLexinetClient &client)
{
	auto request = session.prepare_sync();
	if (!request.has_value()) {
		std::cerr << request.error().error_code;
		return false;
	}
	auto response = client.sync(request.value(), "roundtrip-sync");
	if (!response.has_value()) {
		std::cerr << response.error().error_code;
		return false;
	}
	auto replay = client.sync(request.value(), "roundtrip-replay");
	if (!replay.has_value() ||
	    replay.value().flush_receipt_json != response.value().flush_receipt_json)
		return false;
	auto accepted = session.accept_sync(response.value());
	if (!accepted.has_value())
		std::cerr << accepted.error().error_code;
	return accepted.has_value();
}
bool act(FlashCardSession &s, StudyIntent intent)
{
	return s.apply_intent(intent, StudyInputSource::touch, s.current_binding()).has_value();
}
} // namespace
int main(int argc, char **argv)
{
	if (argc != 5)
		return 2;
	auto runtime = bhp::http_runtime::HttpRuntime::create().value();
	int status = 0;
	{
		bhp::StructuredLogger logger("test.study_roundtrip", std::cerr);
		HttpLexinetConfig config;
		config.lexinet_base_url = argv[1];
		config.maximum_snapshot_age_seconds = 3600;
		std::ifstream ca(argv[2]);
		config.trusted_ca_certificate_pem_for_tests =
		    std::string(std::istreambuf_iterator<char>(ca), {});
		HttpLexinetClient client(runtime, config, logger);
		auto store = std::make_shared<FileStudyStore>(argv[3]);
		FlashCardSession session(store);
		if (!session.restore().has_value())
			return 1;
		if (std::string(argv[4]) == "create" ||
		    std::string(argv[4]) == "crash_after_send" ||
		    std::string(argv[4]) == "crash_after_create" ||
		    std::string(argv[4]) == "resume_create") {
			auto request = session.prepare_create("fixture-default",
							      {"fixture-systems", "fixture-words"});
			if (!request.has_value())
				return 1;
			auto created = client.create_or_resume(request.value(), "roundtrip-create");
			if (!created.has_value())
				return 1;
			if (std::string(argv[4]) == "crash_after_create")
				_exit(89);
			if (!session.install_session(created.value()).has_value() ||
			    !act(session, StudyIntent::flip) ||
			    !act(session, StudyIntent::rate_good))
				return 1;
			if (std::string(argv[4]) == "crash_after_send") {
				auto request = session.prepare_sync();
				if (!request.has_value())
					return 1;
				auto result = client.sync(request.value(), "crash-response-lost");
				if (!result.has_value())
					return 1;
				_exit(90);
			}
			if (!synchronize(session, client) || !session.snapshot().can_undo)
				return 1;
			auto successor = session.prepare_successor();
			if (!successor.has_value()) {
				std::cerr << successor.error().error_code;
				return 1;
			}
			auto second =
			    client.create_or_resume(successor.value(), "roundtrip-successor");
			if (!second.has_value()) {
				std::cerr << second.error().error_code;
				return 1;
			}
			auto repeated = client.create_or_resume(successor.value(),
								"roundtrip-successor-replay");
			if (!repeated.has_value() ||
			    repeated.value().session_id != second.value().session_id)
				return 1;
			if (!session.install_session(second.value()).has_value() ||
			    session.snapshot().availability != StudyAvailability::refill_required ||
			    !session.snapshot().can_undo)
				return 1;
			// An Undo after server materialization remains against the retained
			// completed predecessor.
			if (!act(session, StudyIntent::rating_undo) ||
			    !synchronize(session, client))
				return 1;
			if (session.pending_events().size() != 1)
				return 1; // First replay refreshes predecessor version.
			if (!synchronize(session, client) || !session.pending_events().empty() ||
			    !act(session, StudyIntent::next))
				return 1;
			if (session.snapshot().session_id != second.value().session_id ||
			    !synchronize(session, client))
				return 1;
		} else if (!synchronize(session, client))
			return 1;
		if (!session.pending_events().empty() ||
		    session.snapshot().delivery != StudyDelivery::saved ||
		    !session.predecessor_flush_receipt().has_value())
			return 1;
		std::cout << "{\"saved\":true,\"event_count\":"
			  << ((std::string(argv[4]) == "create" ||
			       std::string(argv[4]) == "resume_create")
				  ? 5
				  : 0)
			  << ",\"simulation_not_hardware_evidence\":true}\n";
	}
	if (!runtime->shutdown().has_value())
		status = 1;
	return status;
}
