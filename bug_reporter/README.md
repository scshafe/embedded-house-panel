# Bug Reporter

<!-- @canonical bug_reporter -->

The panel side of bug reports, step 1 of the accepted
[bug-report design](../docs/architecture/bug-reports-and-self-repair.md).
It snapshots the context of a report, bundles it, queues it on the panel and
sends it to the private inbox on the owner's PC. It never sends a report
anywhere public, opens no audio device, and holds no credential.

## Pieces

- **`log_rings.h`.**
  - `LogRings` (`@canonical bug_reporter_log_rings`): one 512 KiB RAM ring
    per service. Routine logs never touch the eMMC.
  - `FaultRing` (`@canonical bug_reporter_fault_ring`): 256 KiB in two
    halves, written only for faults and exits, with fdatasync, so a crash
    survives a reboot.
- **`ingest.h`** (`@canonical bug_reporter_ingest`): what `bhp-log-tee` sends.
  - A `hello` with the service name first, then log lines, and an `exit`
    notice from the restart wrapper.
  - A fault is a `StructuredLogger::fault()` record: `"level":"error"`,
    `"reportable":"true"` and a dotted upper-case code. Nothing is guessed
    from levels.
- **`report_bundle.h`** (`@canonical bug_report_bundle`), `bhp.bug-report/v1`:
  - **Files:** `report.json`, `logs.jsonl.gz` (the newest lines whose gzip
    fits 512 KiB), and `description.wav` for voice reports (step 3).
  - **The manifest** carries the kind, component, code, call site,
    fingerprint, build (`image-version`), screen and lifecycle tokens, trace
    IDs, and typed text.
  - **Fingerprints:** automatic ones hash component, code and location;
    manual ones are unique.
- **`report_outbox.h`** (`@canonical bug_report_outbox`): the queue
  (`outbox.json`, a DurableFile) and the rate limits.
  - **Bounds:** at most 32 MiB and 50 bundles. The oldest automatic bundle
    makes room. A manual report that still cannot fit is `REPORT.OUTBOX_FULL`,
    never dropped silently.
  - **Automatic reports:** a full bundle for the first of a fingerprint in
    24 h; at most 3 a week per fingerprint, 3 an hour and 10 a day. The rest
    become counts.
  - **Manual reports:** 1 a minute and 20 a day.
  - **Receipt:** a received bundle is deleted, and its status is kept for the
    list.
- **`inbox_client.h`** (`@canonical bug_report_inbox_api`): the inbox's v1
  paths, which the PC receiver (step 4) implements.
  - Resumable chunked file upload, `complete` with the manifest, repeat
    counts, and statuses.
  - Every call is idempotent.
  - There is no token: the inbox knows the panel by its tailnet node
    (ADR 0010).
- **`reporter_protocol.h`** (`@canonical bug_reporter_protocol`,
  `@canonical bug_reporter_client`): the UI's socket.
  - Requests: `snapshot` (the screen at the tap), `submit` (with typed
    text), `cancel` and `status`.
  - The answers carry codes, statuses and dates, never a report's body.
- **`reporter_service.h`** (`@canonical bug_reporter_service`): ties these
  together.
  - **Snapshots** copy the log window at the tap and expire after 10 minutes.
  - **Uploads** back off from 5 s to 1 h.
  - **Statuses** are polled every 15 minutes.
- **`bhp-reporter`:** the process, under its own UID (reserved as
  `bhp-reporter`, 1106).
  - Usage: `--ui-socket PATH --ui-uid UID --ingest-socket PATH
    --ingest-uids UID,... --outbox-dir DIR --fault-dir DIR --inbox-url URL
    [--build-info FILE]`.
  - It logs codes only.
- **`bhp-log-tee`:** `SERVICE 2>&1 | bhp-log-tee --socket PATH --service
  NAME`.
  - It copies each line to its own stdout and to the reporter, never waiting
    on the reporter, and reconnects every 5 s.
  - With `--exit-status N [--signal S] [--restarting]` it sends the restart
    wrapper's exit notice.

## Tests

`ctest --test-dir .build/host-dev -R bug_reporter`:
- the rings;
- the bundle (gzip, bounds, draft rules, fingerprints);
- the outbox (every limit, eviction, and a restart in between);
- the inbox client (a chunk lost mid-send, then resumed; 409; statuses)
  against an in-memory inbox;
- the service end to end:
  - a fault, its repeats as one count, the PR and update-ready status coming
    back;
  - a crash and a restart against an inbox that is down and then up;
  - a typed report with the screen and logs of the moment of the tap;
  - the manual rate limit and snapshot expiry;
- the protocol;
- the daemon and the tee together, against a loopback fake inbox.

## Not done in step 1

- **Packaging.** The init scripts' wiring (each service through
  `bhp-log-tee`, the restart wrapper's exit notice), the Buildroot package
  and the data-partition paths. They come with the image work.
- **Crash frames.** The fatal-signal handler that writes a frame list.
  Today a crash report carries the signal and the logs.
- **Voice reports (step 3):** a submit can name a `bug_report` take
  (`take_id`). With `--handoff-dir` and `--description-channel`, the reporter
  checks its `capture.json` purpose, keeps one channel as a mono 16-bit WAV
  (`description_audio.h`; 60 s is about 1.9 MB), files a `voice` report, and
  deletes the take. It purges takes nobody submitted after 10 minutes. The
  array's 6 channels would not fit the bundle, and the processed channel is
  unmeasured, so the default is channel 0.
- **The report screen** is step 2, in `panel_ui` and the simulator
  ([simulator/README.md](../simulator/README.md#reports)).
