# Plan: The voice-lab Recorder on voice-lab's Contract

<!-- @canonical voice-lab-recorder-wire-plan -->

**Status (2026-10-19):** In progress (issue #23, phase 3, the part that
waited for voice-lab's bundle). voice-lab published
`contracts/corpus-client.v1/` and its fixture server at `8f51656`
(voice-lab#108, #109; voice-lab#86 closed). The owner relayed the pin:
`manifest.json` SHA-256 `84503852…475b` and bundle digest `496302a4…45d1`.
It follows the [recorder groundwork plan](2026-10-19-voice-lab-recorder-groundwork.md),
whose Steps 1–7 are done.

**Goal:** the recorder records a take, packages it as voice-lab's
`corpus-take/v2`, uploads it by the resumable intake, and sends decisions,
all checked against the pinned bundle and, live, against voice-lab's own
fixture server.

**Fixed by voice-lab's bundle (PROTOCOL.md at the pin):**
- Sign-in, refresh and token storage as the groundwork plan's Step 7 built
  them.
- A take is five files: `manifest.json`, `qc.json`, `READY` (written last),
  `original.wav` and `continuity.json`. The take id is
  `take-<SHA-256 of original.wav>`, the profile `xvf3800-linear-4`.
- Every record the panel writes is canonical JSON with one final `\n`,
  byte for byte as the bundle's vectors give it.
- The intake: declare a session, `PUT` each file with `Content-Range` from
  the server's `received_bytes`, finalize, then poll until `accepted` or
  `refused`. Decisions use a stable `req-<32 hex>` and the take's
  `decision_revision`.
- The D-021 standing approval and its hash-chained ledger, with
  `upload-started` written before the first byte.

**Out of scope:** anything on the tailnet. The board is not enrolled yet, so
the board's first sign-in, its D-021 approval and a real upload wait for the
owner (Step 7).

## Decisions

1. **The panel keeps the examples, not the schemas (taken).** As for
   lexinet, the export copies the bundle's examples byte for byte with a
   provenance file, and `--check` verifies the whole bundle at the pin: each
   file's SHA-256 and size as `manifest.json` binds them, nothing unbound,
   and voice-lab's bundle digest.
2. **The live check runs voice-lab's own code (taken).** `make
   verify-voice-lab-live` runs `cole_voice_lab.contract_fixture` from the
   pinned checkout. It uses a venv of CPython 3.12 with the API image's
   hash-locked closure (`requirements/vault-linux.txt`) and serves with a
   throwaway TLS certificate. Like `make verify-lexinet-live`, it is never
   part of `make verify`.

## Steps

- [x] **Step 1: The pin.** (2026-10-19)
  - `scripts/voice_lab/voice_lab_pin.py` (`ContractPin("VOICE_LAB", …)`),
    the exporter `scripts/voice_lab/export_corpus_client_v1_fixtures.py`
    with `--check`, and the provenance and examples under
    `voice_lab_recorder/tests/fixtures/corpus_client_v1/`.
  - An offline fixture test: the pin, every file against its provenance, the
    golden take's WAV rebuilt from its recipe, the records binding each
    other, the continuity rule in integer nanoseconds, and the canonical
    form against the vectors.
  - `docs/interfaces/voice-lab-contract.md`, with the pin and the repin
    commands.
  - Done: the export from the pinned commit gives the relayed manifest SHA-256
    and bundle digest, with 42 bundle files and 18 examples. Exporter tests
    against a synthetic bundle cover a changed, unbound or missing file,
    another contract, an invalid example without its refusal, and a stale
    export. The offline fixture test is part of `make verify`. The `--check`
    fetch needs the network, so it is not.
  - Verify:
    - `python3 scripts/voice_lab/export_corpus_client_v1_fixtures.py --check` exit 0;
    - `ctest --test-dir .build/host-dev -R 'corpus_client_v1'` exit 0;
    - `make verify` exit 0.
- [x] **Step 2: The live check: sign-in and the library.** (2026-10-19)
  - `scripts/voice_lab/live_contract.py` and `make verify-voice-lab-live`:
    the fixture server from the pinned checkout, as Decision 2 says.
  - A live client (`voice_lab_recorder_live_contract_client`) on the
    groundwork's sign-in code and `shared/http_runtime`:
    - device-code sign-in through the fake Pocket ID, with a pending poll first;
    - a refresh that rotates and is saved;
    - the rotated token reused, which revokes the family and signs out;
    - `GET` the catalog and the panel's library view, then a `304` with
      `If-None-Match`;
    - the library without `?profile=`, refused `403 client_scope`.
  - Done: three scenarios, `sign_in`, `library` and `reuse_revokes`, all
    passed against the fixture at the pin. The fixture's root is in the
    temporary directory because voice-lab's corpus store refuses its own
    files on btrfs (`os.listdir()` on an older directory descriptor misses new
    entries there). The report carries checks, counts and error codes, never a
    token.
  - Verify:
    - `python3 -m unittest discover -s scripts/voice_lab/tests` exit 0;
    - `make verify-voice-lab-live` exit 0;
    - `make verify` exit 0.
- [x] **Step 3: Canonical JSON and the take package.** (2026-10-19)
  - A canonical JSON writer that passes every vector, floats included.
  - The packager: from a broker take (its WAV and continuity log) and the
    prompt, catalog and device, the five files, written with `READY` last.
  - The golden take rebuilt byte for byte from the same inputs.
  - Done: `voice_lab_recorder/include/voice_lab_recorder/canonical_json.h` and
    `take_package.h` (`write_take_package`, `inspect_wav`, `qc_metrics`,
    `xvf3800_capture_profile`).
    - nlohmann's `dump()` disagreed with Python's `repr` for 29 of 1,216
      floats (the switch to exponent form at 1e16, and digits that are not
      the shortest), so floats are formatted from `std::to_chars` by Python's
      rule.
    - QC is voice-lab's at the pin, on the primary channel: exact integer
      sums, the same thresholds and findings.
    - The golden take's `manifest.json`, `qc.json`, `READY` and
      `continuity.json` come out byte for byte from a broker take built from
      the examples.
    - The channel map, room and primary channel are inputs. The array reports
      none of them, and which channels are raw on the board is unmeasured.
      An unread serial, firmware string or setting is refused, never filled
      in.
  - Verify:
    - `ctest --test-dir .build/host-dev -R 'canonical_json|take_package'` exit 0;
    - `make verify` exit 0.
- [ ] **Step 4: The intake and decisions.**
  - The intake client on `ResumableUpload`: declare, send from the server's
    offset, finalize, and poll the session.
  - Decisions with stable request IDs, polled to `applied` or `refused`, and
    the library and history with their `ETag`.
  - The live check sends the golden take, interrupts an upload and resumes
    it, keeps the take, and meets a `revision_conflict`.
  - Verify:
    - `ctest --test-dir .build/host-dev -R 'voice_lab_intake|voice_lab_decision'` exit 0;
    - `make verify-voice-lab-live` exit 0;
    - `make verify` exit 0.
- [ ] **Step 5: The D-021 approval and the ledger.**
  - The approval made on the panel's screen (endpoint, node, tag, expiry and
    digest, then a confirmation), kept at `0400`, and its binding.
  - The hash-chained ledger, with `upload-started` before the first byte.
  - The node and tag check before each session, behind a port; the
    `tailscaled` side waits for enrollment.
  - Verify:
    - `ctest --test-dir .build/host-dev -R 'voice_lab_approval|voice_lab_ledger'` exit 0;
    - `make verify` exit 0.
- [ ] **Step 6: The uploader and the outbox.**
  - The uploader process under its own UID, holding the refresh token
    (ADR 0012), with a durable outbox of kept takes.
  - The recorder screens on the uploader's catalog and library.
  - Verify:
    - `ctest --test-dir .build/host-dev -R 'voice_lab_uploader|panel_simulation'` exit 0;
    - `make verify-voice-lab-live` exit 0;
    - `make verify` exit 0.
- [ ] **Step 7: On the board (waits for the owner).**
  - The owner enrolls the panel, approves the first device code from a
    phone, and confirms the D-021 approval on the panel.
  - A spoken take is uploaded, accepted and kept.
  - Verify:
    - a board record under `hardware_tests/results/` of the accepted take;
    - the owner's confirmation that the take is in voice-lab's library.

## Risks

- **The fixture needs CPython 3.12.** The hash-locked closure has 3.12
  wheels only. The live check takes `--python`, or finds `python3.12`.
- **The bundle can move.** A repin is an explicit-checkout export and a
  reviewed diff of the examples, as for lexinet.
