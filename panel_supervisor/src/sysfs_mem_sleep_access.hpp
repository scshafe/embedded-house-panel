#pragma once

#include "panel_supervisor/result.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace house_panel::supervisor::detail {

// @canonical sysfs_mem_sleep_access -- parse and exact selection of the kernel mem_sleep variant.
struct MemSleepVariants {
	/** Variants the kernel lists, in kernel order. */
	std::vector<std::string> available;
	/** The single bracketed variant. */
	std::string selected;
};

/**
 * Parses `/sys/power/mem_sleep` text such as "s2idle [deep]\n".
 *
 * Accepts only the kernel labels `s2idle`, `shallow`, and `deep`, separated by
 * single spaces, with exactly one bracketed selection and an optional final
 * newline. Anything else returns PANEL_SUPERVISOR.MEM_SLEEP_SELECTION_FAILED
 * with failure_stage=parse; nothing is guessed.
 */
[[nodiscard]] Result<MemSleepVariants> parse_mem_sleep_text(std::string_view text);

/**
 * Makes `requested_state` the selected variant at `mem_sleep_path`.
 *
 * Reads and parses the node; writes only when another variant is selected and
 * the requested one is listed; then rereads and requires the requested variant
 * to be selected. Path validation is the caller's job (SysfsSuspend::create).
 */
[[nodiscard]] Result<void> select_mem_sleep_state_at_path(const std::string &mem_sleep_path,
							  const std::string &requested_state);

} // namespace house_panel::supervisor::detail
