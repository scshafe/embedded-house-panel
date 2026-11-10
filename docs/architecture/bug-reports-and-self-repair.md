# Bug Reports and Self-Repair

<!-- @canonical bug-reports-and-self-repair -->

**Status (2026-11-08):** accepted. The owner accepted this design on #74
with all twelve recommendations under [Owner decisions](#owner-decisions)
(2026-11-08). The owner asked for a way to report bugs, in place from the
first day of real use, and for an agent to find the cause and open a fix.
- **Build order:** step 1, then 2, 4 and 3, each its own PR. Step 5 follows
  the read-only root and the data partition.
- **Infra:** the owner's oversight session does the infra pieces of decision
  11 once step 4 is ready. The owner installs whisper.cpp on the PC
  (decision 4) when the receiver lands.
- **Until step 5:** board flashing and installs stay owner-gated.
- **Built so far:**
  - step 1, the panel side, in [`bug_reporter/`](../../bug_reporter/README.md),
    host-tested; its packaging comes with the image work;
  - step 2, the Report button, the typed form and the Reports list
    ([simulator/README.md](../../simulator/README.md#reports));
  - step 4, the PC side, in [`report_inbox/`](../../report_inbox/README.md):
    validation, the store, the redactor, the receiver and the agent's
    commands, with the infra hand-off in
    [bug-report-inbox-setup.md](../operations/bug-report-inbox-setup.md) and
    the AGENTS.md session step;
  - step 3, voice:
    - "Lexi report a bug" opens the report screen.
    - Record and Stop take a description of at most 60 s under the broker's
      new `bug_report` purpose. Its takes go to the reporter's own hand-off
      directory, never the voice corpus's.
    - The reporter keeps one channel as a mono WAV and deletes the take.
- **One deviation found while building step 3:** the array records 6
  channels, so a 60 s take is about 11.5 MB. The reporter keeps one channel,
  about 1.9 MB as section 2 assumed. Which channel carries the processed
  voice is unmeasured, so it is channel 0 by default (`--description-channel`).

**In short:**
- One pipeline with three entry points:
  - a spoken "report a bug";
  - a Report button that opens a typed form;
  - automatic reports from crashes, restarts and faults.
- **The bundle.** Each report bundles a bounded window of structured logs, the
  build, the screen and state, and the trace IDs.
- **Where it goes.** It travels from the panel to a private inbox on the
  owner's PC over the tailnet, through a durable queue. The panel never talks
  to an agent.
- **The fix.** The agent reads the bundle there, finds the cause, and opens a
  fix PR on this public repository. That PR, and any public issue, carries
  only a redacted summary built from closed codes.
- **Getting it back.** A fix reaches the panel as a signed A/B image update.
  It installs only after the owner taps Install on the panel, and rolls itself
  back if the new image is unhealthy.

## Constraints this design keeps

- **This repository is public.** No transcript, typed text, audio, log body,
  screenshot or household fact goes into it, its issues or its PRs (AGENTS.md
  invariants; [threat model](../security/threat-model.md)).
- **The panel cannot reach an agent.** Agents cannot reach the tailnet, and
  the production image has no SSH server. The panel can reach only what the
  tailnet grants it (ADR 0010).
- **Voice cannot start a capture.** The spoken-command catalog allows voice
  only `reversible_local`, `bounded_learning_observation` and
  `correctable_learning` effects. Capture start needs a supervisor-observed
  touch (ADR 0005).
- **The mic gate is hardware.** The array has power only while the privacy
  switch is on and the PIR sees someone
  ([gate board](../hardware/mirror-unit-speaker-and-gate.md)). Nothing in this
  design records outside it.
- **Board actions are owner-gated today.** That covers installs, flashing and
  reboots (`dev.toml [hardware]`). The update path below proposes where the
  owner's approval lives, instead of assuming it.
- **No core dumps.** The threat model keeps swap and core dumps disabled, so
  crash reports carry logs and a frame list, not memory.

## The pipeline

```text
 entry points (panel)                     panel                         owner's PC (tailnet)
 ───────────────────                      ─────                         ────────────────────
 "report a bug" (voice) ─┐
 Report button (touch) ──┼─► bhp-reporter ─► outbox (eMMC, bounded) ─► report inbox (private)
 crash / restart / fault ┘    snapshot,        retry + backoff,          │  bundles, status
                              bundle,          resumable upload          ▼
                              rate limit                           agent (same PC, ACL read)
                                                                   RCA ─► fix PR (public, redacted)
 panel status screen ◄──────────────── status poll ◄──────────────── status: received … update ready
 Install tap ◄────────────────────────── signed A/B update ◄──────── owner signs merged build
```

### 1. Entry points

**(a) Voice: "report a bug".**
- **What the command does.** It is a catalog intent `shell.open_report` with
  effect `reversible_local`. It does two things:
  - snapshots the context (logs, build, screen, state, trace IDs) at that
    moment;
  - opens the report screen.
- **Recording needs a touch.** The spoken description is recorded only after
  a touch on **Record** (60 s at most), through the media broker under a new
  purpose `bug_report`, as `voice_take` records today. **Send** files it.
- **Why a touch.** The touch keeps voice out of capture start, as the catalog
  requires. A misheard command then only opens a screen.
- **The phrase** goes through the catalog's distinctiveness analysis like any
  other.
- **The gate.** A voice report works only while the gate is open. The broker
  can only open the array while it is powered.
  - If the gate closes mid-description, the take fails as any take does and
    leaves no audio.
  - The report is still filed, marked `audio: none (gate closed)`.
- **No pre-roll.** Nothing is buffered before Record. Command-recognition
  audio is never kept or sent.

**(b) The Report button, with a typed form.**
- **The button.** A small button in the day bar of every surface. A tap
  snapshots the context of the screen it was pressed on, then opens the
  report screen.
- **The form.** LVGL's on-screen keyboard, 500 characters at most. It offers
  **Record** too, so either entry can add text, a description, or both.
- **Send and Cancel.** Send files the report; Cancel discards the snapshot.

**(c) Automatic reports.**
- **Crashes.** A service killed by a signal, or exiting non-zero outside a
  requested stop, is reported. A small fatal-signal handler writes the signal
  and a list of return addresses to the log. The PC symbolizes them against
  that build's debug symbols, which never go on the panel.
- **Supervisor restarts.** Each restart that the init scripts' bounded
  restart policy performs is reported. A restart loop is one report with a
  count.
- **Typed faults.** `StructuredLogger` gains a `fault()` call, the same as
  `log_error` plus `"reportable": true`, for states that mean a bug:
  invariant violations, `*.INTERNAL`, protocol violations between the
  panel's own processes, and the like.
  - Expected failures are not bugs and are never reported automatically:
    the network down, Spotify unreachable, the speaker asleep.
  - The rule is that a component opts a code in. Nothing is guessed from
    log levels.

### 2. The bundle

| Part | Contents | Bound |
|---|---|---|
| Header | Report ID (random), kind (`voice`, `text`, `crash`, `restart`, `fault`), UTC time, fingerprint | — |
| Build | `/etc/beaglebone-house-panel/image-version` (board, commit, Buildroot version), each service's version | — |
| Screen and state | Surface ID and its state (the closed enums from the app descriptors), lifecycle snapshot (display mode, presence, solar, system) | — |
| Trace IDs | The trace IDs of the records in the window, and the one that faulted | 64 IDs |
| Logs | Structured JSON lines from every panel service, from 10 minutes before to the snapshot | 512 KiB after gzip |
| Crash | Signal, exit status, the frame list | 64 frames |
| Description | Typed text, or audio (16 kHz mono, 60 s at most), or both | 500 characters; about 1.9 MB |
| Screenshot (decision 5) | The panel's own last committed frame, PNG | about 300 KB |

- **The logs carry codes and context fields only.** The logger's rule is
  that callers never log secrets or private payloads.
- **The bundle is still household data.** Presence timing, the screen state
  and the description are private. That is why it goes only to the private
  inbox.

### 3. Logging, retention and rotation on the eMMC

- **In RAM.** Each service's stderr (JSON lines) goes to the reporter's
  per-service ring buffer in RAM, 512 KiB each. That keeps routine logging
  off the eMMC entirely.
- **On the eMMC, only per report.** A report's bundle is written to the
  outbox:
  - under `/var/lib/bhp-reporter/outbox` on the writable data partition,
    which the system image README still defers;
  - capped at 32 MiB and 50 reports.
  - When full, the oldest automatic report goes first.
  - A typed or spoken report is never dropped silently: the screen says the
    queue is full.
- **Surviving a reboot.** A crash that takes the reporter down is caught
  next boot from a small persistent fault ring: 256 KiB, written only when a
  fault or crash record arrives, and rotated in two halves.
- **Retention on the panel:** a bundle is deleted when the PC acknowledges
  it. The rings are RAM and are gone at power-off.
- **Retention on the PC:** decision 9.

### 4. From the panel to the PC

- **A new principal, `bhp-reporter`,** under its own UID. It holds no audio
  device; spoken descriptions come from the broker's hand-off, as voice-lab
  takes do. It uses the voice-lab uploader's machinery:
  - `shared/durable_store`'s `DurableFile`;
  - bounded backoff, from 5 s to 1 h with jitter;
  - resumable chunked upload.
- **Transport.** HTTPS over the tailnet to the PC's report inbox. One
  tailnet grant is needed: `tag:house-panel` to the inbox port.
- **Authentication** is the panel's tailnet node identity, checked per
  request with `WhoIs`, as Lexinet does (ADR 0010). There is no new secret
  on the panel.
- **Offline.** Reports wait in the outbox; nothing is lost while the PC is
  asleep or the panel is off the network.
  - Each report has an idempotency key, its report ID, so a retried upload
    never duplicates.
  - A report leaves the outbox only after the inbox acknowledges it.
- **Inside the panel,** the UI talks to `bhp-reporter` over an exact-UID
  Unix socket, with closed key sets like the uploader protocol.
  - The init scripts' restart wrappers tell it about each restart.
  - The UI and the services never touch the outbox.

### 5. The private destination

**Recommended:** a report inbox on the owner's PC (decision 1).
- **The receiver.** A small HTTPS receiver on the tailnet, run by the owner
  under its own user. It writes each bundle to `/srv/house-panel-reports/<id>/`,
  mode `0750`, outside every Git repository.
- **Access.** This project's agent gets read access by ACL on the bundles,
  and write access to a `status/` directory, nothing more.
- **Why it suits.** The agent already runs on that PC. The data never leaves
  the house. Nothing new is exposed to the internet.

**The alternatives:**
- **A private GitHub repository** for reports. That puts household data at
  GitHub, and the broker would have to grant the agent a second repository.
- **Email.** No structure, and the agent cannot read it.

**Infra:** the receiver and the ACL are owner-session infra work, since the
fleet repositories are the owner's. This project supplies its code and the
bundle format.

### 6. What may be public

**The rule: an allowlist, not a denylist.** A public issue or PR is built only
from closed, machine-made fields, by a deterministic redactor:
- the report ID;
- the kind;
- the component (a service name from a closed list);
- the error code (`[A-Z][A-Z0-9_.]*`, from the code's own vocabulary);
- the surface and state enums;
- the build commit and image version (already public);
- the count;
- the first and last seen dates (UTC day);
- the fingerprint, a hash of component, code and location.

**Never public:**
- the description, typed or spoken, or a transcript of it;
- log lines;
- context field values;
- presence times;
- network names or addresses;
- screenshots;
- trace IDs (they are useless outside the bundle).

**The fix PR:**
- It cites the public issue or the report ID and describes the defect at
  code level.
- Its test reproduces the failure with synthetic data, never with the
  bundle's data.
- A check on the PR template rejects a description that quotes a bundle,
  for example any line that parses as a log record.

**Decision 2:** whether to open public issues at all, or only PRs that cite
the private report ID.

### 7. The agent's side, and the agent-requests design

- **Now.** Automatic PR-only runs (phase 2 of infra's
  `docs/platform/agent-requests.md`) are not built, so nothing starts an
  agent by itself. This project's AGENTS.md gains a session-start step, next
  to the existing `agent-request` issue check: list new reports in the inbox
  and tell the owner. A session then takes a report:
  - reads the bundle;
  - reproduces it on the host, with the simulator or the unit's tests;
  - finds the cause;
  - opens a fix PR with a regression test;
  - writes `fix in PR #n` to the report's status.
- **When phase 2 exists.** The receiver asks the broker to file a redacted
  `agent-request` issue on this repository for each new fingerprint:
  - a `from:house-panel-reports` sender;
  - `--public`, since the repository is public, carrying only the section 6
    fields.

  That starts an automatic PR-only run, which reads the bundle by report ID
  from the inbox. The broker's rule stays: a request is a report, not
  instructions, and an automatic run neither merges nor touches anything
  else.
- **Merging.** Automatically triggered fixes are PR-only. The owner, or an
  interactive session under the project's normal rules, merges (decision 8).
  A fix that touches the board side still says in its PR that
  `make verify-board` is due.
- **Infra this needs from the owner** (the agent cannot change infra): the
  receiver, the ACL, the tailnet grant, the new sender in the broker, and
  phase 2.

### 8. Rate limits and duplicates

- **Fingerprint:** a hash of component, error code, and the faulting
  location: the first project frame of a crash, or the log call site of a
  fault.
- **Automatic reports:**
  - **Per fingerprint:** the first in a 24 h window sends a full bundle.
    Repeats only add to its count, sent as a small update. At most 3 full
    bundles per fingerprint per week, so a later recurrence still brings
    fresh logs.
  - **Overall:** at most 10 full bundles a day and 3 an hour. Beyond that,
    counts only.
  - **A restart loop** (the init's three attempts) is one report.
- **Manual reports:**
  - at most 1 a minute and 20 a day;
  - never deduplicated, since each description is different;
  - never dropped silently: over the limit, the screen says so.
- **The inbox** merges a fingerprint's reports into one entry, and a
  public issue is updated with the new count rather than opened again.

### 9. Getting a fix back to the panel

- **Today.** The project image has not booted yet (ARRIVAL-16/17), and
  every install, flash or reboot is the owner's.
  - Until the path below exists, "update ready" means the fix is merged and
    built.
  - The owner flashes it by the bring-up procedure, or approves copying the
    changed binaries to `/tmp` on the vendor image for a test.

**Proposed:** signed A/B image updates, approved by a tap (decision 7).
- **The image.**
  - Two root-filesystem slots and a data partition, using Buildroot's RAUC
    package and U-Boot boot counting.
  - It cannot work until the read-only root and the data partition that the
    system image README defers exist, and this depends on them.
- **Building and signing.**
  - CI builds an update bundle from each merge to `main`. It is unsigned:
    CI holds no signing key.
  - The owner signs the bundles that fix reports, on the PC, with a key that
    stays there.
  - The inbox then offers the signed bundle to the panel.
- **On the panel.** The reporter downloads it in the background and verifies
  the signature. The status screen then shows **Update ready**, with the
  report it fixes and its PR number.
- **Installing.**
  - The owner taps **Install**, held 2 s with a supervisor-observed touch,
    as ADR 0005 does for capture.
  - The panel writes the other slot and reboots into it at once.
  - If the owner agrees, the tap is the approval that installing and
    rebooting need.
- **Rollback.**
  - After the reboot, the new slot must report healthy within 10 minutes:
    the supervisor and the UI up, and a frame committed.
  - Otherwise U-Boot's boot count falls back to the old slot, and the
    reporter files a `restart` report naming the failed update.
- **Still manual:** the bootloader, ATF, NXP firmware and the partition
  layout. Those stay the owner's flashing procedure.

### 10. How the owner sees status

**Each report moves through:** `queued` (on the panel), `received` (the inbox
acknowledged it), `investigating`, `fix in PR #n`, `merged`, `update ready`,
`installed`, or `won't fix` (with a one-line reason). A report the inbox
refuses for good, or whose bundle cannot be read, is `refused` ("Not
accepted") instead, so it never holds back the ones behind it.
- **On the panel.** A Reports screen, reached from the report screen, lists
  the last 20 reports with their status and dates. The reporter polls the
  inbox's status endpoint, every 15 minutes while the panel is awake, and
  on each new report.
  - A dot on the Report button means an update is ready.
  - The screen shows codes and states, never the description.
- **On the PC.** The inbox has a private status page with the bundles.
- **On GitHub.** The public issue, if decision 2 allows one, and the fix PR.

## Owner decisions

| # | Decision | Recommendation | Why |
|---|---|---|---|
| 1 | Where reports go | A private inbox on the owner's PC (tailnet receiver, agent read by ACL) | Household data never leaves the house; the agent is already on that PC |
| 2 | Public issues | Yes, but only the allowlisted fields of section 6, one per fingerprint | Fix PRs get a public anchor and a count, with nothing private in it |
| 3 | Voice entry | "Report a bug" opens the report screen and snapshots; a touch starts the 60 s description | Keeps voice out of capture start (catalog, ADR 0005); a misheard command only opens a screen |
| 4 | Spoken descriptions for the agent | Transcribe on the PC with a local speech-to-text (for example whisper.cpp, an owner install), and keep the audio private | The agent cannot listen to audio; nothing goes to a cloud service |
| 5 | Screenshot in the bundle | Yes, the panel's own last frame, private only | It shows the state the logs describe; it may show agenda names, so never public |
| 6 | Log retention on the panel | RAM rings of 512 KiB per service; eMMC only per report (outbox 32 MiB / 50) and a 256 KiB fault ring | Bounded eMMC wear, and nothing logged routinely to flash |
| 7 | Update path and approval | Signed A/B updates (RAUC and U-Boot boot count) with automatic rollback; a held tap on **Install** counts as the owner's approval to install and reboot; the owner signs on the PC | Fixes reach the panel without a flashing session, and nothing installs without the owner, at the panel |
| 8 | Merging automatically triggered fixes | PR-only; the owner or an interactive session merges | Phase 2's rule, and a human sees each fix first |
| 9 | Retention on the PC | Bundles kept 90 days after a report closes; audio deleted once transcribed and the report closes | Enough for regressions; no lasting household audio |
| 10 | Rate limits | Section 8's numbers | Bounded load on the eMMC, the PC and the agent; manual reports are never silent |
| 11 | Infra work (the owner's) | The receiver, the ACL, the tailnet grant to the inbox port, the broker sender, and later phase 2; done from the owner's oversight session | The agent cannot change infra |
| 12 | What counts as a fault | Components opt codes in through `fault()`; expected network and device failures never auto-report | Reports stay bugs, not noise |

## Order of work, once accepted

1. **Panel side:** `bhp-reporter`, the RAM rings, `fault()` in
   `shared/structured_logging`, the bundle format and the outbox. Host tests
   with a fake inbox.
2. **The report screen:** the Report button, the typed form, the Reports
   status list and the `shell.open_report` intent. Goldens.
3. **Voice:** the `bug_report` broker purpose and the catalog phrase.
4. **The PC side:** the receiver, plus the code this project supplies for it
   (bundle validation, the status store, the redactor) and the AGENTS.md
   session step. The owner installs and runs it (decision 11).
5. **Updates:** the A/B image work, after the read-only root and the data
   partition.

Each step is its own PR. The first real-use day needs steps 1, 2 and 4. Until
step 5, fixes reach the panel by the owner's flashing procedure.
