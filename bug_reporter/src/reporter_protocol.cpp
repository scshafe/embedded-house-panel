#include "bug_reporter/reporter_protocol.h"
#include "bhp/media_window_protocol/media_window_protocol.h"
#include <algorithm>
#include <cstring>
#include <nlohmann/json.hpp>
#include <regex>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace bug_reporter::protocol
{
namespace
{
using Json = nlohmann::json;

template <typename Value> Result<Value> fail(const std::string &where)
{
	return Result<Value>::failure({"REPORT.PROTOCOL_INVALID", "The reporter frame is not valid", {{"where", where}}, false});
}

bool token(const std::string &value)
{
	static const std::regex pattern("[a-z0-9_.-]{0,32}");
	return std::regex_match(value, pattern);
}

bool code(const std::string &value)
{
	static const std::regex pattern("[A-Z][A-Z0-9_]*(\\.[A-Z0-9_]+)+");
	return value.size() <= 96 && std::regex_match(value, pattern);
}

bool one_of(const std::string &value, std::initializer_list<const char *> allowed)
{
	return std::any_of(allowed.begin(), allowed.end(), [&value](const char *item) { return value == item; });
}

bool exact_keys(const Json &value, std::initializer_list<const char *> keys)
{
	if (!value.is_object() || value.size() != keys.size())
		return false;
	return std::all_of(keys.begin(), keys.end(), [&value](const char *key) { return value.contains(key); });
}

bool text_ok(const std::string &text)
{
	if (text.size() > 2000)
		return false;
	return std::all_of(text.begin(), text.end(), [](unsigned char c) { return (c >= 0x20 && c != 0x7f) || c == '\n'; });
}

bool date(const std::string &value)
{
	static const std::regex pattern(R"(\d{4}-\d{2}-\d{2})");
	return std::regex_match(value, pattern);
}

bool valid(const ListedReport &item)
{
	return valid_report_id(item.report_id) && one_of(item.kind, {"voice", "text", "crash", "restart", "fault"}) &&
	       one_of(item.status, {"queued", "received", "investigating", "fix_in_pr", "merged", "update_ready",
				    "installed", "wont_fix"}) &&
	       date(item.created_on);
}

Result<std::string> finish(Json value, const char *type)
{
	value["type"] = type;
	value["version"] = kProtocolVersion;
	std::string text;
	try {
		text = value.dump(-1, ' ', false, Json::error_handler_t::strict) + "\n";
	} catch (const Json::exception &) {
		return fail<std::string>(std::string("encode_") + type + "_utf8");
	}
	if (text.size() > kMaximumFrameBytes)
		return fail<std::string>(std::string("encode_") + type + "_size");
	return Result<std::string>::success(std::move(text));
}

Result<Json> parse(std::string_view frame, const char *where)
{
	if (frame.empty() || frame.size() > kMaximumFrameBytes || frame.back() != '\n' ||
	    frame.find('\n') != frame.size() - 1)
		return fail<Json>(where);
	Json value = Json::parse(frame.substr(0, frame.size() - 1), nullptr, false);
	if (value.is_discarded() || !value.is_object() || !value.contains("type") || !value["type"].is_string() ||
	    !value.contains("version") || !value["version"].is_number_unsigned() || value["version"] != kProtocolVersion)
		return fail<Json>(where);
	return Result<Json>::success(std::move(value));
}

bool read(const Json &value, const char *key, std::string &out)
{
	if (!value.contains(key) || !value[key].is_string())
		return false;
	out = value[key].get<std::string>();
	return true;
}
} // namespace

bool valid_snapshot_id(const std::string &snapshot_id)
{
	static const std::regex pattern("snap-[0-9a-f]{16}");
	return std::regex_match(snapshot_id, pattern);
}

Result<std::string> encode(const UiRequest &request)
{
	if (const auto *snapshot = std::get_if<SnapshotRequest>(&request)) {
		const auto &s = snapshot->screen;
		for (const auto *field : {&s.surface, &s.state, &s.display_mode, &s.presence, &s.solar, &s.system})
			if (!token(*field))
				return fail<std::string>("encode_snapshot");
		return finish({{"surface", s.surface}, {"state", s.state}, {"display_mode", s.display_mode},
			       {"presence", s.presence}, {"solar", s.solar}, {"system", s.system}},
			      "snapshot");
	}
	if (const auto *submit = std::get_if<SubmitRequest>(&request)) {
		if (!valid_snapshot_id(submit->snapshot_id) || !text_ok(submit->text))
			return fail<std::string>("encode_submit");
		return finish({{"snapshot_id", submit->snapshot_id}, {"text", submit->text}}, "submit");
	}
	if (const auto *cancel = std::get_if<CancelRequest>(&request)) {
		if (!valid_snapshot_id(cancel->snapshot_id))
			return fail<std::string>("encode_cancel");
		return finish({{"snapshot_id", cancel->snapshot_id}}, "cancel");
	}
	return finish(Json::object(), "status");
}

Result<std::string> encode(const ReporterEvent &event)
{
	if (const auto *taken = std::get_if<SnapshotTaken>(&event)) {
		if (!valid_snapshot_id(taken->snapshot_id))
			return fail<std::string>("encode_snapshot_taken");
		return finish({{"snapshot_id", taken->snapshot_id}}, "snapshot_taken");
	}
	if (const auto *submitted = std::get_if<Submitted>(&event)) {
		if (!valid_report_id(submitted->report_id))
			return fail<std::string>("encode_submitted");
		return finish({{"report_id", submitted->report_id}}, "submitted");
	}
	if (const auto *refused = std::get_if<Refused>(&event)) {
		if (!one_of(refused->request, {"snapshot", "submit", "cancel"}) || !code(refused->error_code))
			return fail<std::string>("encode_refused");
		return finish({{"request", refused->request}, {"error_code", refused->error_code}}, "refused");
	}
	const auto &reports = std::get<Reports>(event);
	if (reports.items.size() > kMaximumListed)
		return fail<std::string>("encode_reports_size");
	Json items = Json::array();
	for (const auto &item : reports.items) {
		if (!valid(item))
			return fail<std::string>("encode_reports_item");
		items.push_back({{"report_id", item.report_id},
				 {"kind", item.kind},
				 {"status", item.status},
				 {"pr", item.pr ? Json(*item.pr) : Json(nullptr)},
				 {"created_on", item.created_on}});
	}
	return finish({{"items", items}, {"update_ready", reports.update_ready}}, "reports");
}

Result<UiRequest> decode_ui_request(std::string_view frame)
{
	auto parsed = parse(frame, "decode_ui_request");
	if (!parsed.has_value())
		return Result<UiRequest>::failure(parsed.error());
	const Json &value = parsed.value();
	const std::string type = value["type"];
	UiRequest request = StatusRequest{};
	bool ok = false;
	if (type == "snapshot") {
		SnapshotRequest snapshot;
		auto &s = snapshot.screen;
		ok = exact_keys(value, {"type", "version", "surface", "state", "display_mode", "presence", "solar", "system"}) &&
		     read(value, "surface", s.surface) && read(value, "state", s.state) &&
		     read(value, "display_mode", s.display_mode) && read(value, "presence", s.presence) &&
		     read(value, "solar", s.solar) && read(value, "system", s.system);
		request = snapshot;
	} else if (type == "submit") {
		SubmitRequest submit;
		ok = exact_keys(value, {"type", "version", "snapshot_id", "text"}) && read(value, "snapshot_id", submit.snapshot_id) &&
		     read(value, "text", submit.text);
		request = submit;
	} else if (type == "cancel") {
		CancelRequest cancel;
		ok = exact_keys(value, {"type", "version", "snapshot_id"}) && read(value, "snapshot_id", cancel.snapshot_id);
		request = cancel;
	} else if (type == "status")
		ok = exact_keys(value, {"type", "version"});
	if (!ok || !encode(request).has_value())
		return fail<UiRequest>("decode_ui_request");
	return Result<UiRequest>::success(request);
}

Result<ReporterEvent> decode_reporter_event(std::string_view frame)
{
	auto parsed = parse(frame, "decode_reporter_event");
	if (!parsed.has_value())
		return Result<ReporterEvent>::failure(parsed.error());
	const Json &value = parsed.value();
	const std::string type = value["type"];
	ReporterEvent event = Reports{};
	bool ok = false;
	if (type == "snapshot_taken") {
		SnapshotTaken taken;
		ok = exact_keys(value, {"type", "version", "snapshot_id"}) && read(value, "snapshot_id", taken.snapshot_id);
		event = taken;
	} else if (type == "submitted") {
		Submitted submitted;
		ok = exact_keys(value, {"type", "version", "report_id"}) && read(value, "report_id", submitted.report_id);
		event = submitted;
	} else if (type == "refused") {
		Refused refused;
		ok = exact_keys(value, {"type", "version", "request", "error_code"}) && read(value, "request", refused.request) &&
		     read(value, "error_code", refused.error_code);
		event = refused;
	} else if (type == "reports") {
		Reports reports;
		ok = exact_keys(value, {"type", "version", "items", "update_ready"}) && value["items"].is_array() &&
		     value["update_ready"].is_boolean() && value["items"].size() <= kMaximumListed;
		if (ok)
			reports.update_ready = value["update_ready"].get<bool>();
		for (std::size_t at = 0; ok && at < value["items"].size(); ++at) {
			const Json &item = value["items"][at];
			ListedReport listed;
			ok = exact_keys(item, {"report_id", "kind", "status", "pr", "created_on"}) &&
			     read(item, "report_id", listed.report_id) && read(item, "kind", listed.kind) &&
			     read(item, "status", listed.status) && read(item, "created_on", listed.created_on) &&
			     (item["pr"].is_null() || item["pr"].is_number_unsigned());
			if (ok && item["pr"].is_number_unsigned())
				listed.pr = item["pr"].get<std::uint64_t>();
			reports.items.push_back(listed);
		}
		event = reports;
	}
	if (!ok || !encode(event).has_value())
		return fail<ReporterEvent>("decode_reporter_event");
	return Result<ReporterEvent>::success(event);
}

struct ReporterClient::Channel {
	explicit Channel(int descriptor) : frames(descriptor, kMaximumFrameBytes) {}
	bhp::media_window_protocol::FrameChannel frames;
};

ReporterClient::ReporterClient(int descriptor) : channel_(std::make_unique<Channel>(descriptor)) {}
ReporterClient::~ReporterClient() = default;

Result<std::unique_ptr<ReporterClient>> ReporterClient::connect(const std::string &path, std::uint32_t reporter_uid)
{
	using Output = Result<std::unique_ptr<ReporterClient>>;
	if (path.empty() || path[0] != '/' || path.size() >= sizeof(sockaddr_un::sun_path))
		return Output::failure({"REPORT.REPORTER_SOCKET_INVALID", "The reporter socket path is invalid", {}, false});
	const int descriptor = ::socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
	sockaddr_un address{};
	address.sun_family = AF_UNIX;
	std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
	if (descriptor < 0 || ::connect(descriptor, reinterpret_cast<sockaddr *>(&address), sizeof address) != 0) {
		if (descriptor >= 0)
			::close(descriptor);
		return Output::failure({"REPORT.REPORTER_UNAVAILABLE", "The reporter socket did not accept", {}, true});
	}
	return for_connected_peer(descriptor, reporter_uid);
}

Result<std::unique_ptr<ReporterClient>> ReporterClient::for_connected_peer(int descriptor, std::uint32_t reporter_uid)
{
	using Output = Result<std::unique_ptr<ReporterClient>>;
	const auto uid = bhp::media_window_protocol::peer_uid(descriptor);
	if (!uid || *uid != reporter_uid) {
		::close(descriptor);
		return Output::failure({"REPORT.REPORTER_PEER_REJECTED", "The socket's peer is not the reporter UID", {}, false});
	}
	return Output::success(std::unique_ptr<ReporterClient>(new ReporterClient(descriptor)));
}

bool ReporterClient::connected() const
{
	return channel_->frames.open();
}

Result<bool> ReporterClient::send(const UiRequest &request)
{
	auto frame = encode(request);
	if (!frame.has_value())
		return Result<bool>::failure(frame.error());
	if (!channel_->frames.send(frame.value()))
		return Result<bool>::failure({"REPORT.REPORTER_UNAVAILABLE", "The reporter went away", {}, true});
	return Result<bool>::success(true);
}

Result<std::vector<ReporterEvent>> ReporterClient::poll()
{
	using Output = Result<std::vector<ReporterEvent>>;
	std::vector<ReporterEvent> events;
	for (int frames = 0; frames < 16; ++frames) {
		auto received = channel_->frames.receive();
		if (!received)
			return Output::failure({"REPORT.REPORTER_UNAVAILABLE", "The reporter went away", {}, true});
		if (!received.value())
			break;
		auto event = decode_reporter_event(*received.value());
		if (!event.has_value()) {
			channel_->frames.close();
			return Output::failure({"REPORT.REPORTER_UNAVAILABLE", "The reporter sent a malformed frame", {}, true});
		}
		events.push_back(std::move(event.value()));
	}
	return Output::success(std::move(events));
}
} // namespace bug_reporter::protocol
