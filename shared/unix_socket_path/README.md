# Unix Socket Path

<!-- @canonical unix_socket_path -->

Owns the one rule for a listening Unix socket's path left behind by a killed
daemon: `bhp::unix_socket_path::clear_stale_socket`.

- It removes the path only when it is a socket owned by the effective user,
  `connect()` to it is refused (nobody listens), and its device and inode are
  unchanged after that probe.
- It never follows a link and never removes anything else: a regular file, a
  directory, another user's socket or a live listener stays, and the caller's
  `bind()` then fails as before.
- Every daemon that binds a socket in a runtime directory calls it just
  before `bind()`, so a restart after a crash binds again instead of running
  without its socket.

It depends only on the C library.
