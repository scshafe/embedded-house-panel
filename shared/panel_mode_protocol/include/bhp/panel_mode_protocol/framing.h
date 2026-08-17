#pragma once

#include "bhp/panel_mode_protocol/result.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace bhp::panel_mode_protocol {

// @canonical panel_mode_protocol_framer -- bounded incremental JSON-Lines stream framing.
class FrameAccumulator final {
public:
	/** Accepts arbitrary stream chunks and returns every complete LF-terminated frame. */
	[[nodiscard]] Result<std::vector<std::string>> append(std::string_view bytes);

	/** Reports clean EOF only when no unterminated bytes remain buffered. */
	[[nodiscard]] Result<bool> finish() const;

	/** Discards connection-local state after disconnect, timeout, or protocol failure. */
	void reset() noexcept;

	[[nodiscard]] std::size_t buffered_bytes() const noexcept;

private:
	std::string buffer_;
};

} // namespace bhp::panel_mode_protocol
