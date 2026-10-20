# voice-lab Recorder

The panel app that records takes for voice-lab's training corpus (issue #23).
Its plans are the
[recorder groundwork plan](../.claude/plans/2026-10-19-voice-lab-recorder-groundwork.md)
(done) and the [recorder wire plan](../.claude/plans/2026-10-19-voice-lab-recorder-wire.md).

## voice-lab's contract

voice-lab's `contracts/corpus-client.v1/` bundle is pinned at voice-lab
`8f51656` ([the voice-lab contract](../docs/interfaces/voice-lab-contract.md)).
Its examples sit in `tests/fixtures/corpus_client_v1/`, exported byte for byte
by `scripts/voice_lab/export_corpus_client_v1_fixtures.py`, and
`voice_lab_corpus_client_v1_contract_fixture_test` checks them offline.
`make verify-voice-lab-live` signs in and reads the library against
voice-lab's own fixture server (`tools/live_contract_client.cpp`).

## Sign-in and the refresh token (Step 7)

`pocket_id_sign_in.h` holds the uploader's credential code
([ADR 0012](../docs/decisions/0012-keep-a-rotating-voice-lab-refresh-token-on-the-panel.md)).

- **`discover`** reads the issuer's OpenID discovery document. It needs the
  same issuer, the device-code grant, and HTTPS endpoints on the issuer's
  origin.
- **`DeviceCodeSignIn`** runs RFC 8628 for `voice-lab-panel`.
  - It sends `client_id`, plus an optional scope, and never a `resource`.
  - The prompt holds only `verification_uri` and the user code.
  - It polls no faster than the interval and stops at `expires_in` without
    asking, because Pocket ID v2.13 keeps answering `authorization_pending`.
- **`RefreshTokenStore`** keeps the one stored secret in a `DurableFile`.
  - The file is `refresh-token.json`, 0600, in an owner-only directory.
  - It records the issuer and client the token belongs to, and never offers a
    token saved for another.
- **`VoiceLabSession`** hands out access tokens.
  - Refresh is single-flight: one exchange while other callers wait.
  - The rotated refresh token is written durably before the new access token
    is used. If that write fails, the session signs out instead.
  - `invalid_grant` signs out and clears the store.

Tests: `voice_lab_device_code_token_store_test`, against a fake Pocket ID that
behaves as voice-lab's D-056 records v2.13.0, and the live check against
voice-lab's fake Pocket ID. A sign-in to the real Pocket ID needs the panel on
the tailnet.
