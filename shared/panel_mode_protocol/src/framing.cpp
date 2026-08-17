#include "bhp/panel_mode_protocol/framing.h"

#include "bhp/panel_mode_protocol/types.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bhp::panel_mode_protocol {
namespace {

Error framing_error(const ErrorCode code, std::string message)
{
	return Error{code, std::move(message), {{"operation", "frame_accumulator"}}};
}

} // namespace

Result<std::vector<std::string>> FrameAccumulator::append(const std::string_view bytes)
{
	std::vector<std::string> complete_frames;
	for (const char byte : bytes) {
		if (buffer_.size() >= kMaximumFrameBytes) {
			return Result<std::vector<std::string>>::failure(framing_error(
				ErrorCode::FrameTooLarge,
				"Protocol stream frame exceeds the maximum byte count"));
		}
		buffer_.push_back(byte);
		if (byte == '\n') {
			complete_frames.push_back(std::move(buffer_));
			buffer_.clear();
		}
	}
	return Result<std::vector<std::string>>::success(std::move(complete_frames));
}

Result<bool> FrameAccumulator::finish() const
{
	if (!buffer_.empty()) {
		return Result<bool>::failure(framing_error(
			ErrorCode::FrameEncodingInvalid,
			"Protocol stream ended with an unterminated frame"));
	}
	return Result<bool>::success(true);
}

void FrameAccumulator::reset() noexcept { buffer_.clear(); }

std::size_t FrameAccumulator::buffered_bytes() const noexcept { return buffer_.size(); }

} // namespace bhp::panel_mode_protocol
