#include "voice_lab_recorder/corpus_client.h"
#include "bhp/durable_store/durable_store.h"
#include "voice_lab_recorder/canonical_json.h"
#include <algorithm>
#include <cerrno>
#include <fcntl.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace voice_lab_recorder
{
namespace
{
using bhp::http_runtime::HttpMethod;
using bhp::http_runtime::HttpRequest;
using Json = nlohmann::json;
constexpr const char *kProfileQuery = "?profile=xvf3800-linear-4";
// The five files of corpus-take/v2, in sorted order.
const char *const kPackageFiles[] = {"READY", "continuity.json", "manifest.json", "original.wav", "qc.json"};
constexpr unsigned kResyncLimit = 4;

Error api_error(const char *code, const char *message, const long status, const std::string &refusal, bool retryable)
{
	Error error{code, message, {{"status", std::to_string(status)}}, retryable};
	if (!refusal.empty())
		error.context["error"] = refusal;
	return error;
}

std::string refusal_code(const Json &body)
{
	return body.is_object() && body.contains("error") && body["error"].is_string() ? body["error"].get<std::string>()
										       : std::string();
}

/** PROTOCOL.md section 7, for an answer that is not one the call accepts. */
Error classify(const long status, const Json &body)
{
	const auto refusal = refusal_code(body);
	const bool retryable = status == 408 || status == 429 || status >= 500 ||
			       (status == 409 && refusal == "session_busy");
	if (retryable)
		return api_error("VOICE_LAB.API_RETRYABLE", "voice-lab asked to try again later", status, refusal, true);
	if (status == 401)
		return api_error("VOICE_LAB.API_UNAUTHORIZED", "voice-lab refused the token; sign in again", status, refusal,
				 false);
	return api_error("VOICE_LAB.API_REFUSED", "voice-lab refused the request", status, refusal, false);
}

/** One path segment, percent-encoded (RFC 3986 unreserved characters stay). */
std::string segment(const std::string &value)
{
	static const char *digits = "0123456789ABCDEF";
	std::string out;
	for (const unsigned char c : value) {
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.' ||
		    c == '_' || c == '~') {
			out += static_cast<char>(c);
		} else {
			out += '%';
			out += digits[c >> 4];
			out += digits[c & 15];
		}
	}
	return out;
}

std::string sha256_file(const std::string &path, std::uint64_t &size)
{
	const int descriptor = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
	if (descriptor < 0)
		return {};
	EVP_MD_CTX *context = EVP_MD_CTX_new();
	bool ok = context != nullptr && EVP_DigestInit_ex(context, EVP_sha256(), nullptr) == 1;
	std::vector<unsigned char> buffer(1 << 16);
	size = 0;
	while (ok) {
		const ssize_t got = ::read(descriptor, buffer.data(), buffer.size());
		if (got < 0 && errno == EINTR)
			continue;
		if (got <= 0) {
			ok = got == 0;
			break;
		}
		size += static_cast<std::uint64_t>(got);
		ok = EVP_DigestUpdate(context, buffer.data(), static_cast<std::size_t>(got)) == 1;
	}
	unsigned char digest[EVP_MAX_MD_SIZE];
	unsigned int length = 0;
	ok = ok && EVP_DigestFinal_ex(context, digest, &length) == 1 && length == 32;
	EVP_MD_CTX_free(context);
	::close(descriptor);
	if (!ok)
		return {};
	static const char *hex = "0123456789abcdef";
	std::string text;
	for (unsigned at = 0; at < length; ++at) {
		text += hex[digest[at] >> 4];
		text += hex[digest[at] & 15];
	}
	return text;
}

std::optional<Json> file_progress(const Json &status, const std::string &path)
{
	if (!status.is_object() || !status.contains("files") || !status["files"].is_array())
		return std::nullopt;
	for (const auto &file : status["files"])
		if (file.is_object() && file.value("path", std::string()) == path)
			return file;
	return std::nullopt;
}

bhp::durable_store::Error to_upload_error(const Error &error)
{
	return {error.code, error.message, error.context, error.retryable};
}
} // namespace

Result<Json> intake_session_request(const std::string &package_directory)
{
	Json files = Json::array();
	std::string manifest_sha256;
	for (const char *name : kPackageFiles) {
		std::uint64_t size = 0;
		const auto digest = sha256_file(package_directory + "/" + name, size);
		if (digest.empty())
			return Result<Json>::failure({"VOICE_LAB.TAKE_PACKAGE_INVALID", "A package file is unreadable", {}, false});
		files.push_back({{"path", name}, {"sha256", digest}, {"size_bytes", size}});
		if (std::string(name) == "manifest.json")
			manifest_sha256 = digest;
	}
	const auto slash = package_directory.find_last_of('/');
	const std::string take_id = slash == std::string::npos ? package_directory : package_directory.substr(slash + 1);
	if (take_id.rfind("take-", 0) != 0)
		return Result<Json>::failure({"VOICE_LAB.TAKE_PACKAGE_INVALID", "A package is named by its take id", {}, false});
	return Result<Json>::success({{"schema_version", "cole-voice-lab.api.intake-session/v1"},
				      {"session_id", take_id},
				      {"manifest_sha256", manifest_sha256},
				      {"files", files}});
}

Result<std::string> new_request_id()
{
	unsigned char bytes[16];
	if (RAND_bytes(bytes, sizeof bytes) != 1)
		return Result<std::string>::failure({"VOICE_LAB.RANDOM_UNAVAILABLE", "No random request id", {}, false});
	static const char *hex = "0123456789abcdef";
	std::string id = "req-";
	for (const unsigned char byte : bytes) {
		id += hex[byte >> 4];
		id += hex[byte & 15];
	}
	return Result<std::string>::success(id);
}

CorpusClient::CorpusClient(CorpusClientSettings settings, ApiTransport transport, AccessTokens tokens)
	: settings_(std::move(settings)), transport_(std::move(transport)), tokens_(std::move(tokens))
{
}

Result<CorpusClient::Exchange> CorpusClient::exchange(HttpRequest request, const bool transfer)
{
	using Output = Result<Exchange>;
	request.url = settings_.api_url + request.url;
	request.budget = transfer ? settings_.transfer_budget : settings_.request_budget;
	request.trusted_ca_pem_for_tests = settings_.trusted_ca_pem_for_tests;
	request.headers.emplace_back("Accept", "application/json");
	const auto base_headers = request.headers;
	for (int attempt = 0; attempt < 2; ++attempt) {
		auto token = tokens_.current();
		if (!token.has_value())
			return Output::failure(token.error());
		request.headers = base_headers;
		request.headers.emplace_back("Authorization", "Bearer " + token.value());
		auto answer = transport_(request);
		if (!answer.has_value())
			return Output::failure({"VOICE_LAB.API_RETRYABLE", "voice-lab did not answer",
						{{"http_code", answer.error().code}}, answer.error().retryable});
		Exchange exchange{answer.value().status, answer.value().etag, Json()};
		if (!answer.value().body.empty()) {
			exchange.body = Json::parse(answer.value().body, nullptr, false);
			if (exchange.body.is_discarded())
				exchange.body = Json();
		}
		// A 401 refreshes the access token once (PROTOCOL.md section 1).
		if (exchange.status == 401 && attempt == 0) {
			tokens_.invalidate();
			continue;
		}
		return Output::success(std::move(exchange));
	}
	return Output::failure(api_error("VOICE_LAB.API_UNAUTHORIZED", "voice-lab refused the token; sign in again", 401,
					 {}, false));
}

Result<ViewAnswer> CorpusClient::view(const std::string &path, const std::string &etag)
{
	HttpRequest request;
	request.method = HttpMethod::Get;
	request.url = path;
	if (!etag.empty())
		request.headers.emplace_back("If-None-Match", etag);
	auto answer = exchange(std::move(request), false);
	if (!answer.has_value())
		return Result<ViewAnswer>::failure(answer.error());
	const auto &got = answer.value();
	if (got.status == 304)
		return Result<ViewAnswer>::success({true, etag, Json()});
	if (got.status != 200)
		return Result<ViewAnswer>::failure(classify(got.status, got.body));
	if (!got.body.is_object())
		return Result<ViewAnswer>::failure(
			api_error("VOICE_LAB.API_ANSWER_INVALID", "voice-lab's answer is not a JSON object", 200, {}, false));
	return Result<ViewAnswer>::success({false, got.etag, got.body});
}

Result<Json> CorpusClient::json_call(const HttpMethod method, const std::string &path, const std::string &body,
				     const std::initializer_list<long> accepted)
{
	HttpRequest request;
	request.method = method;
	request.url = path;
	if (method == HttpMethod::Post) {
		request.headers.emplace_back("Content-Type", "application/json");
		request.body = body;
	}
	const bool transfer = path.size() > 9 && path.compare(path.size() - 9, 9, "/finalize") == 0;
	auto answer = exchange(std::move(request), transfer);
	if (!answer.has_value())
		return Result<Json>::failure(answer.error());
	const auto &got = answer.value();
	if (std::find(accepted.begin(), accepted.end(), got.status) == accepted.end())
		return Result<Json>::failure(classify(got.status, got.body));
	if (!got.body.is_object())
		return Result<Json>::failure(
			api_error("VOICE_LAB.API_ANSWER_INVALID", "voice-lab's answer is not a JSON object", got.status, {},
				  false));
	return Result<Json>::success(got.body);
}

Result<ViewAnswer> CorpusClient::catalog(const std::string &etag)
{
	return view("/api/v1/catalog", etag);
}

Result<ViewAnswer> CorpusClient::library(const std::string &etag)
{
	return view(std::string("/api/v1/corpus/library") + kProfileQuery, etag);
}

Result<ViewAnswer> CorpusClient::history(const std::string &prompt_id, const std::string &etag)
{
	return view("/api/v1/corpus/prompts/" + segment(prompt_id) + "/takes" + kProfileQuery, etag);
}

Result<Json> CorpusClient::declare(const std::string &package_directory)
{
	auto request = intake_session_request(package_directory);
	if (!request.has_value())
		return request;
	auto body = canonical_json(request.value());
	if (!body.has_value())
		return Result<Json>::failure(body.error());
	return json_call(HttpMethod::Post, "/api/v1/intake/sessions", body.value(), {200, 201});
}

Result<Json> CorpusClient::session(const std::string &take_id)
{
	return json_call(HttpMethod::Get, "/api/v1/intake/sessions/" + segment(take_id), {}, {200});
}

Result<Json> CorpusClient::finalize(const std::string &take_id)
{
	return json_call(HttpMethod::Post, "/api/v1/intake/sessions/" + segment(take_id) + "/finalize", {}, {200});
}

Result<bool> CorpusClient::send_file(const std::string &package_directory, const std::string &take_id,
				     const std::string &path)
{
	using Output = Result<bool>;
	const int descriptor = ::open((package_directory + "/" + path).c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
	struct stat status {};
	if (descriptor < 0 || ::fstat(descriptor, &status) != 0) {
		if (descriptor >= 0)
			::close(descriptor);
		return Output::failure({"VOICE_LAB.TAKE_PACKAGE_INVALID", "A package file is unreadable", {}, false});
	}
	const auto total = static_cast<std::uint64_t>(status.st_size);
	const std::string url = "/api/v1/intake/sessions/" + segment(take_id) + "/files/" + segment(path);

	bhp::durable_store::UploadPorts ports;
	ports.server_offset = [this, &take_id, &path]() -> bhp::durable_store::Result<std::uint64_t> {
		auto current = session(take_id);
		if (!current.has_value())
			return bhp::durable_store::Result<std::uint64_t>::failure(to_upload_error(current.error()));
		const auto file = file_progress(current.value(), path);
		if (!file || !(*file)["received_bytes"].is_number_unsigned())
			return bhp::durable_store::Result<std::uint64_t>::failure(
				{"VOICE_LAB.API_ANSWER_INVALID", "The session does not list this file", {}, false});
		return bhp::durable_store::Result<std::uint64_t>::success(
			file->value("complete", false) ? file->value("size_bytes", std::uint64_t{0})
						       : (*file)["received_bytes"].get<std::uint64_t>());
	};
	ports.put_range = [this, &url, descriptor, total](std::uint64_t offset,
							  std::uint64_t length) -> bhp::durable_store::Result<bhp::durable_store::Done> {
		HttpRequest request;
		request.method = HttpMethod::Put;
		request.url = url;
		request.headers = {{"Content-Type", "application/octet-stream"},
				   {"Content-Range", total == 0 ? std::string("bytes */0")
								: "bytes " + std::to_string(offset) + "-" +
									  std::to_string(offset + length - 1) + "/" +
									  std::to_string(total)}};
		request.file_body = bhp::http_runtime::FileRange{descriptor, offset, length};
		auto answer = exchange(std::move(request), true);
		using Sent = bhp::durable_store::Result<bhp::durable_store::Done>;
		if (!answer.has_value())
			return Sent::failure(to_upload_error(answer.error()));
		if (answer.value().status == 200 || answer.value().status == 202)
			return Sent::success({});
		auto refused = classify(answer.value().status, answer.value().body);
		const auto code = refusal_code(answer.value().body);
		// The server says where it stands: resynchronise from its session.
		if (code == "range_not_contiguous" || code == "file_already_received" || code == "checksum_mismatch") {
			refused.code = "VOICE_LAB.UPLOAD_RESYNC";
			refused.retryable = true;
		}
		return Sent::failure(to_upload_error(refused));
	};
	auto upload = bhp::durable_store::ResumableUpload::create(total, std::max<std::uint64_t>(1, settings_.chunk_bytes),
								  std::move(ports));
	if (!upload.has_value()) {
		::close(descriptor);
		return Output::failure({upload.error().code, upload.error().message, upload.error().context, false});
	}
	auto driver = upload.value();
	unsigned resyncs = 0;
	for (;;) {
		auto step = driver.step();
		if (step.has_value()) {
			if (step.value().complete) {
				::close(descriptor);
				return Output::success(true);
			}
			continue;
		}
		const auto &error = step.error();
		// A server that discarded a file (checksum_mismatch) moves its offset back,
		// which the driver refuses; the caller's next send_file starts from it.
		if (error.code == "VOICE_LAB.UPLOAD_RESYNC" && ++resyncs <= kResyncLimit)
			continue;
		::close(descriptor);
		return Output::failure({error.code == "VOICE_LAB.UPLOAD_RESYNC" ? "VOICE_LAB.API_RETRYABLE" : error.code,
					error.message, error.context, error.retryable || error.code == "VOICE_LAB.UPLOAD_RESYNC"});
	}
}

Result<Json> CorpusClient::upload(const std::string &package_directory)
{
	auto declared = declare(package_directory);
	if (!declared.has_value())
		return declared;
	const std::string take_id = declared.value().value("session_id", std::string());
	if (declared.value().value("state", std::string()) != "open")
		return declared;
	for (const char *name : kPackageFiles) {
		const auto file = file_progress(declared.value(), name);
		if (file && file->value("complete", false))
			continue;
		auto sent = send_file(package_directory, take_id, name);
		if (!sent.has_value())
			return Result<Json>::failure(sent.error());
	}
	return finalize(take_id);
}

Result<Json> CorpusClient::decide(const std::string &take_id, const DecisionRequest &request)
{
	const Json body = {{"schema_version", "cole-voice-lab.api.take-decision-request/v1"},
			   {"request_id", request.request_id},
			   {"decision", request.decision},
			   {"expected_revision", request.expected_revision},
			   {"reason", request.reason ? Json(*request.reason) : Json(nullptr)}};
	auto text = canonical_json(body);
	if (!text.has_value())
		return Result<Json>::failure(text.error());
	return json_call(HttpMethod::Post, "/api/v1/corpus/takes/" + segment(take_id) + "/decision", text.value(), {202});
}

Result<Json> CorpusClient::request_status(const std::string &request_id)
{
	return json_call(HttpMethod::Get, "/api/v1/corpus/requests/" + segment(request_id), {}, {200});
}
} // namespace voice_lab_recorder
