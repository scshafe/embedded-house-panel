# Panel Mode Protocol

Owns the canonical, process-neutral codec for lifecycle protocol version 2.
Both `panel_supervisor` and `panel_ui` use this library; neither process may
maintain a private serializer, digest preimage, enum mapping, or parser.
The shared `FrameAccumulator` is likewise the only stream framer: it accepts
arbitrary socket chunks, enforces the per-frame bound before unbounded buffering,
and distinguishes clean EOF from an unterminated frame.

The wire transport is one bounded UTF-8 JSON object followed by one line-feed.
JSON is transport only. `snapshot_id` is SHA-256 over the typed binary preimage
specified in `docs/interfaces/panel-mode-protocol.md`, so whitespace and object
member order cannot change the binding. Strings in version 2 are deliberately
ASCII-constrained identifiers; the strict decoder rejects escapes, duplicate or
unknown members, coercions, and partial/default objects.

`@canonical panel_mode_protocol_codec` returns structured `{code, message,
context}` errors. Context names the failed field, bound, or operation but never
contains a raw frame, release identity, session ID, challenge, or digest.

OpenSSL EVP supplies SHA-256. The digest is a replay/content binding, not peer
authentication: callers must still enforce exact Unix peer identity, a fresh
connection challenge, the one-latest-pending rule, and deadline/disconnect
invalidation.
