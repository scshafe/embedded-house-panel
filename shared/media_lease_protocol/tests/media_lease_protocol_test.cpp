#include "bhp/media_lease_protocol/media_lease_protocol.h"
#include <iostream>
using namespace bhp::media_lease_protocol;
namespace
{
int failures = 0;
void expect(bool value, const std::string &label)
{
	if (!value) {
		++failures;
		std::cerr << "FAIL: " << label << '\n';
	}
}
const PresentationBinding kBinding{2, std::string(32, 'b'), 7, "sha256:" + std::string(64, 'c'), std::string(32, 'd'), 3};
const std::string kLease = "lease-0000000000000000000001";
const std::string kRequest = "request-000000000000000001";

void test_requests()
{
	ArmRequest arm{kRequest, Purpose::VoiceTake, "observation-00000000000001", kBinding, "prompt:0042"};
	auto frame = encode(UiRequest(arm));
	auto decoded = decode_ui_request(frame.value());
	expect(decoded && std::get<ArmRequest>(decoded.value()).operation_ref == "prompt:0042" &&
		       std::get<ArmRequest>(decoded.value()).purpose == Purpose::VoiceTake,
	       "arm round trip");
	auto other = arm;
	other.purpose = Purpose::WishRecording;
	auto wish = decode_ui_request(encode(UiRequest(other)).value());
	expect(wish && std::get<ArmRequest>(wish.value()).purpose == Purpose::WishRecording,
	       "other purposes are wire-valid; the broker decides");
	auto start = decode_ui_request(encode(UiRequest(StartRequest{kLease})).value());
	expect(start && std::get<StartRequest>(start.value()).lease_id == kLease, "start round trip");
	const std::string take = "take-" + std::string(64, 'a');
	auto discard = decode_ui_request(encode(UiRequest(DiscardRequest{take})).value());
	expect(discard && std::get<DiscardRequest>(discard.value()).take_id == take, "discard round trip");
	expect(!encode(UiRequest(DiscardRequest{"take-xyz"})), "discard names a take ID");
	auto discarded = decode_broker_event(encode(BrokerEvent(Discarded{take})).value());
	expect(discarded && std::get<Discarded>(discarded.value()).take_id == take, "discarded round trip");
	auto stop = decode_ui_request(encode(UiRequest(StopRequest{kLease})).value());
	expect(stop && std::holds_alternative<StopRequest>(stop.value()), "stop round trip");
	auto bad = arm;
	bad.operation_ref = "has space";
	expect(!encode(UiRequest(bad)), "operation_ref is printable without spaces");
	expect(!decode_ui_request("{\"lease_id\":\"" + kLease + "\",\"type\":\"start\",\"version\":1,\"lease_id\":\"x\"}\n"),
	       "duplicate key");
	expect(!decode_ui_request("{\"audio\":\"AAAA\",\"lease_id\":\"" + kLease + "\",\"type\":\"start\",\"version\":1}\n"),
	       "unknown key");
	expect(!decode_ui_request("{\"lease_id\":\"" + kLease + "\",\"type\":\"start\",\"version\":2}\n"), "version");
	expect(!decode_ui_request("{\"lease_id\":\"" + kLease + "\",\"type\":\"pause\",\"version\":1}\n"), "unknown type");
}

void test_events()
{
	auto armed = decode_broker_event(encode(BrokerEvent(Armed{kRequest, kLease})).value());
	expect(armed && std::get<Armed>(armed.value()).lease_id == kLease, "armed");
	auto refused = decode_broker_event(encode(BrokerEvent(Refused{kRequest, "MEDIA_BROKER.WINDOW_CLOSED"})).value());
	expect(refused && std::get<Refused>(refused.value()).error_code == "MEDIA_BROKER.WINDOW_CLOSED", "refused");
	auto level = decode_broker_event(encode(BrokerEvent(Level{kLease, 300, {-200, -1200, 0}})).value());
	expect(level && std::get<Level>(level.value()).peak_decibels_tenths[2] == 0, "level");
	expect(!encode(BrokerEvent(Level{kLease, 300, {5}})), "a level above full scale");
	expect(!encode(BrokerEvent(Level{kLease, 300, std::vector<std::int32_t>(9, -10)})), "too many channels");
	Ended committed{kLease, Outcome::Committed, "take-" + std::string(64, 'a'), "requested", 16000, {}};
	auto ended = decode_broker_event(encode(BrokerEvent(committed)).value());
	expect(ended && std::get<Ended>(ended.value()).frames == 16000, "committed end");
	Ended failed{kLease, Outcome::Failed, {}, {}, 0, "MEDIA_CAPTURE.XRUN"};
	auto failure = decode_broker_event(encode(BrokerEvent(failed)).value());
	expect(failure && std::get<Ended>(failure.value()).error_code == "MEDIA_CAPTURE.XRUN", "failed end");
	auto mixed = committed;
	mixed.error_code = "X";
	expect(!encode(BrokerEvent(mixed)), "a committed end carries no error");
	auto wrong_reason = committed;
	wrong_reason.stop_reason = "window";
	expect(!encode(BrokerEvent(wrong_reason)), "stop reason is requested or maximum-duration");
}
} // namespace

int main()
{
	test_requests();
	test_events();
	std::cout << "media_lease_protocol: " << failures << " failures\n";
	return failures ? 1 : 0;
}
