#pragma once

#include <string>

namespace bhp::unix_socket_path {

/** What clear_stale_socket found at a listening socket's path. */
enum class StalePath {
	Absent,  ///< nothing there: bind may proceed
	Removed, ///< a socket this user owned with nobody listening; it was unlinked
	InUse,   ///< a listener answered: another instance is running
	Foreign, ///< not a socket, a link, or another user's socket: left alone
	Failed,  ///< the path could not be inspected or removed
};

/**
 * Removes a stale Unix socket left by a killed daemon so the next bind succeeds.
 *
 * Only a socket owned by this effective user, whose connect() is refused, and
 * whose device and inode are unchanged after the probe is unlinked. Links are
 * never followed and nothing else is ever removed; the bind then fails as before.
 */
StalePath clear_stale_socket(const std::string &path) noexcept;

const char *to_string(StalePath outcome) noexcept;

} // namespace bhp::unix_socket_path
