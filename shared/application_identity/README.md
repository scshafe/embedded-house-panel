# Application Identity

Owns the one compile-time `appliance_release_id` shared by every application
binary in an appliance release. The top-level CMake configure derives it from
the first line of `VERSION` and the full Git commit. When the source worktree
differs from that commit, the suffix is `-dirty.<sha256>` over every Git-visible
source file, so divergent dirty builds cannot share a generic marker. A source
archive or Buildroot package must pass the already-derived identity explicitly;
it may not invent a fallback.

`@canonical application_identity` is build provenance, not a secret. The first
lifecycle exchange establishes the handshake, and both peers revalidate the
identity in every later snapshot and acknowledgement; any mismatch stays dark.
The accepted atomic-cutover contract requires supervisor, `panel_ui`, and image
packaging to carry the exact same value.
