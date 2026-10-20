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

## The take package (wire plan Step 3)

`take_package.h` turns one committed media-broker take (`original.wav` and
`capture.json`) into voice-lab's `cole-voice-lab.corpus-take/v2`:
- the five files are `original.wav` unchanged, `continuity.json`,
  `manifest.json`, `qc.json`, and `READY`, which is written last;
- the package is assembled in `.incoming-<take id>`, renamed into place, and
  written once;
- every record is canonical JSON (`canonical_json.h`), byte for byte as the
  bundle's vectors give it;
- `qc.json` measures the primary channel as voice-lab's QC does. The PC
  recomputes it anyway;
- `xvf3800_capture_profile` builds the capture profile from the broker's
  device record and the board's channel map, room and primary channel.

Test: `voice_lab_canonical_json_take_package_test` rebuilds the golden take
from the pinned examples and compares every file.

## The intake and decisions (wire plan Step 4)

`corpus_client.h` is the panel's side of PROTOCOL.md sections 4–7:
- **The intake.** Declare the session, send each file with `Content-Range`
  from the bytes the server holds (`ResumableUpload`), finalize, and read
  the session until it is `accepted` or `refused`.
- **Decisions.** Each carries a stable `req-` id and the take's revision.
  Poll the request until it is `applied` or `refused`.
- **The views.** The catalog, the panel's library and a prompt's history,
  each read with its `ETag`.

A `401` refreshes the token once, then means sign in again. Every other
answer is retryable or a refusal, as section 7 lists. Tests:
- `voice_lab_intake_decision_client_test` runs against an in-memory intake;
- `make verify-voice-lab-live` runs against voice-lab's own server.

## The D-021 approval and the ledger (wire plan Step 5)

`home_host.h` keeps the owner's standing approval of the PC, as voice-lab's
`home_host.py` does:
- **The approval** names the endpoint, the tailnet, the node's stable id
  and `tag:voice-lab`, for at most 366 days. It is stored write-once at
  `0400` once its digest has been reviewed, and `binding.json` names it.
- **`check_peer`** checks the node that answers against the approval
  before every session.
- **The ledger** is a hash chain: `upload-started` is written before the
  first byte, then `upload-completed` (with the receipt) or
  `upload-refused`.

Test: `voice_lab_approval_ledger_test` reproduces the bundle's approval,
binding and ledger examples byte for byte.

## The outbox (wire plan Step 6)

`outbox.h` is the uploader's durable queue of kept takes:
- `keep` packages a take the broker handed off, records it in `outbox.json`,
  and removes the hand-off copy.
- `step` moves the oldest take one action along:
  `queued`, `uploaded`, `accepted`, `kept`, or `refused`.
- Every attempt first checks the D-021 approval and the node, and writes the
  ledger.
- The `kept` request id is saved before it is sent.
- Retryable answers back off. A refusal ends that take. Signed out, no
  approval, or another node blocks the queue, with the reason in `status()`.

Test: `voice_lab_uploader_outbox_test` runs it against an in-memory
voice-lab. `make verify-voice-lab-live` runs it against voice-lab's own
server.

## The uploader process (wire plan Step 7)

`bhp-voice-lab-uploader` runs `UploaderService` (`uploader_service.h`) under
its own UID, and serves the UI's socket with the uploader protocol
(`uploader_protocol.h`, Decision 4).
- **The socket:** exact-UID peers in both directions, closed key sets, and
  frames up to 64 KiB.
- **What the UI sends:** `status`, `sign_in`, `approve` (with the reviewed
  digest) and `keep` (a handed-off take, for a prompt).
- **What the uploader answers:** `state` whenever something changed, or
  `refused`.
- **What runs in the background:**
  - the device-code sign-in;
  - the D-021 approval preview for the node `tailscale status --json`
    reports, kept stable for ten minutes so the reviewed digest stays the
    one confirmed;
  - the catalog and the panel's library, read with their `ETag`s;
  - the outbox.

Tests:
- `voice_lab_uploader_protocol_test` and `voice_lab_uploader_service_test`;
- the daemon's usage test;
- the live check's `uploader` scenario, which drives the service by protocol
  requests alone, from sign-in to a missing prompt shown as kept.

## On the FRDM demo (wire plan Step 8)

`scripts/deploy/frdm_panel_demo.sh start 0|180 --voice-lab-config FILE`
runs `bhp-voice-lab-uploader` on the board.
- **Where it runs:** as UID 912, with hand-off group 913, from
  `/tmp/bhp-demo`.
- **The file:** `voice-lab.json` holds the issuer, `api_url`, `endpoint`,
  `tailnet_suffix` and, once the array's channel map is measured,
  `board_profile`. It holds no credential, but it names the tailnet, so keep
  it out of Git.
- **Off the tailnet** the recorder shows Sign in, and the sign-in reports
  Pocket ID unreachable.
- **Without `board_profile`** a kept take stays in the hand-off directory,
  because Keep is refused (`VOICE_LAB.CAPTURE_PROFILE_UNMEASURED`).
