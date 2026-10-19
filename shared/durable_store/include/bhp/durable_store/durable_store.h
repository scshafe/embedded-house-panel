#pragma once
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <variant>

/**
 * Outbox mechanics shared by the panel's domains (issue #23): an atomic durable
 * file, a bounded exponential backoff, and a resumable chunked upload driver.
 * What to store, what to send, credentials and retry policy stay with each
 * domain; nothing here knows a study or a take.
 */
namespace bhp::durable_store
{
struct Error {
	std::string code;
	std::string message;
	std::map<std::string, std::string> context;
	/** A transport or server condition worth retrying after a backoff. */
	bool retryable{false};
};

template <typename Value> class Result final
{
public:
	static Result success(Value value) { return Result(std::move(value)); }
	static Result failure(Error error) { return Result(std::move(error)); }
	bool has_value() const { return std::holds_alternative<Value>(storage_); }
	explicit operator bool() const { return has_value(); }
	const Value &value() const { return std::get<Value>(storage_); }
	const Error &error() const { return std::get<Error>(storage_); }

private:
	explicit Result(Value value) : storage_(std::move(value)) {}
	explicit Result(Error error) : storage_(std::move(error)) {}
	std::variant<Value, Error> storage_;
};

enum class CommitBoundary { temporary_opened, payload_written, file_synchronized, renamed, directory_synchronized };

/** The file names and error-code prefix a domain gives its durable file. */
struct DurableFileNames {
	std::string lock;
	std::string state;
	std::string temporary;
	/** Errors are `<prefix>.STORAGE_*`, e.g. STUDY.STORAGE_FULL. */
	std::string error_prefix;
};

/**
 * @canonical durable_file -- one owner-only, bounded, atomically replaced file.
 *
 * The directory is created 0700 and must be owned by this user with no group
 * or other access; an exclusive `flock` on the lock file keeps one writer per
 * directory. A commit writes a temporary file, fsyncs it, renames it over the
 * state file and fsyncs the directory. A leftover temporary file is never read
 * as committed data. A failed directory fsync is sticky
 * (`<prefix>.STORAGE_COMMIT_UNCERTAIN`) until the process restarts.
 */
class DurableFile final
{
public:
	DurableFile(std::string directory, DurableFileNames names, std::size_t maximum_bytes,
		    std::function<void(CommitBoundary)> boundary_observer = {});
	~DurableFile();
	DurableFile(const DurableFile &) = delete;
	DurableFile &operator=(const DurableFile &) = delete;
	Result<std::optional<std::string>> read();
	Result<bool> commit(const std::string &payload);

private:
	struct Implementation;
	std::unique_ptr<Implementation> implementation_;
};

struct BackoffPolicy {
	std::chrono::milliseconds initial{1000};
	std::chrono::milliseconds maximum{300000};
	/** Each delay spreads uniformly over [delay * (1 - jitter), delay]; 0..1. */
	double jitter{0.2};
};

/**
 * @canonical bounded_backoff -- doubling delays from `initial` up to `maximum`.
 * Randomness is injected (a value in [0, 1)) so tests are deterministic.
 */
class Backoff final
{
public:
	/** DURABLE.BACKOFF_INVALID for a zero, inverted or out-of-range policy. */
	static Result<Backoff> create(BackoffPolicy policy, std::function<double()> random01);
	std::chrono::milliseconds next_delay();
	void reset() { failures_ = 0; }
	unsigned failures() const { return failures_; }

private:
	Backoff(BackoffPolicy policy, std::function<double()> random01)
		: policy_(policy), random01_(std::move(random01))
	{
	}
	BackoffPolicy policy_;
	std::function<double()> random01_;
	unsigned failures_{0};
};

struct Done {
};

/** What the upload driver asks of its domain's transport. */
struct UploadPorts {
	/** The bytes the server already holds for this file, from its own status. */
	std::function<Result<std::uint64_t>()> server_offset;
	/** Sends [offset, offset + length) of the file. */
	std::function<Result<Done>(std::uint64_t offset, std::uint64_t length)> put_range;
};

struct UploadState {
	std::uint64_t server_offset{0};
	bool complete{false};
};

/**
 * @canonical resumable_upload -- one file, sent in chunks, resumed from the server.
 *
 * Each `step()` is one network action. The driver learns where to start from
 * the server's status, never from local belief, and asks again after any
 * failed send, because the server may have kept part of it. A retryable error
 * is returned for the caller to back off; a refusal stops the upload for good
 * (`stopped()`). A server offset beyond the file, or one that moves backwards,
 * is UPLOAD.SERVER_OFFSET_INVALID and stops it.
 */
class ResumableUpload final
{
public:
	/** UPLOAD.CONFIG_INVALID for a zero chunk or ports left empty. */
	static Result<ResumableUpload> create(std::uint64_t total_bytes, std::uint64_t chunk_bytes, UploadPorts ports);
	Result<UploadState> step();
	bool complete() const { return known_ && offset_ == total_; }
	bool stopped() const { return stopped_; }

private:
	ResumableUpload(std::uint64_t total, std::uint64_t chunk, UploadPorts ports)
		: total_(total), chunk_(chunk), ports_(std::move(ports))
	{
	}
	std::uint64_t total_;
	std::uint64_t chunk_;
	UploadPorts ports_;
	std::uint64_t offset_{0};
	bool known_{false};
	bool stopped_{false};
};
} // namespace bhp::durable_store
