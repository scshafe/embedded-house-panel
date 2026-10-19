#include "bhp/durable_store/durable_store.h"
#include <algorithm>
#include <cmath>
namespace bhp::durable_store
{
Result<Backoff> Backoff::create(BackoffPolicy policy, std::function<double()> random01)
{
	if (policy.initial.count() <= 0 || policy.maximum < policy.initial ||
	    policy.maximum > std::chrono::hours(24) || !(policy.jitter >= 0.0 && policy.jitter <= 1.0) ||
	    !random01)
		return Result<Backoff>::failure(
			{"DURABLE.BACKOFF_INVALID", "Backoff needs 0 < initial <= maximum <= 24 h and jitter in [0, 1]", {}});
	return Result<Backoff>::success(Backoff(policy, std::move(random01)));
}

std::chrono::milliseconds Backoff::next_delay()
{
	// initial * 2^failures, capped; computed in double so a long outage cannot overflow.
	const double base = std::min(static_cast<double>(policy_.maximum.count()),
				     static_cast<double>(policy_.initial.count()) * std::pow(2.0, std::min(failures_, 62U)));
	++failures_;
	const double draw = std::clamp(random01_(), 0.0, 1.0);
	const double delay = base * (1.0 - policy_.jitter * draw);
	return std::chrono::milliseconds(std::max<long long>(1, std::llround(delay)));
}

Result<ResumableUpload> ResumableUpload::create(std::uint64_t total_bytes, std::uint64_t chunk_bytes,
						UploadPorts ports)
{
	if (chunk_bytes == 0 || !ports.server_offset || !ports.put_range)
		return Result<ResumableUpload>::failure(
			{"UPLOAD.CONFIG_INVALID", "Upload needs a chunk size and both transport ports", {}});
	return Result<ResumableUpload>::success(ResumableUpload(total_bytes, chunk_bytes, std::move(ports)));
}

Result<UploadState> ResumableUpload::step()
{
	using Output = Result<UploadState>;
	if (stopped_)
		return Output::failure({"UPLOAD.STOPPED", "Upload was refused and stays stopped", {}});
	if (!known_) {
		auto reported = ports_.server_offset();
		if (!reported) {
			if (!reported.error().retryable)
				stopped_ = true;
			return Output::failure(reported.error());
		}
		const auto offset = reported.value();
		// The server never loses accepted bytes or holds more than the file.
		if (offset > total_ || offset < offset_) {
			stopped_ = true;
			return Output::failure({"UPLOAD.SERVER_OFFSET_INVALID",
						"Server reports an offset outside the file or behind an earlier one",
						{{"server_offset", std::to_string(offset)}, {"total_bytes", std::to_string(total_)}}});
		}
		offset_ = offset;
		known_ = true;
		return Output::success({offset_, offset_ == total_});
	}
	if (offset_ == total_)
		return Output::success({offset_, true});
	const auto length = std::min(chunk_, total_ - offset_);
	auto sent = ports_.put_range(offset_, length);
	if (!sent) {
		// The server may have kept part of the chunk: ask again before the next send.
		known_ = false;
		if (!sent.error().retryable)
			stopped_ = true;
		return Output::failure(sent.error());
	}
	offset_ += length;
	return Output::success({offset_, offset_ == total_});
}
} // namespace bhp::durable_store
