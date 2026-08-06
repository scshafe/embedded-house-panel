#pragma once

#include "panel_supervisor/error_code.hpp"
#include "panel_supervisor/result.hpp"

#include <string>

namespace house_panel::supervisor::detail {

// @canonical exact_file_writer -- shared privileged-file write path for runtime adapters.
[[nodiscard]] Result<void> write_exact_file(const std::string &path, const std::string &content,
					    ErrorCode error_code, const std::string &operation);

} // namespace house_panel::supervisor::detail
