# Project Review and Enrichment Ideas, November 2026

<!-- @canonical review-and-enrichment-2026-11 -->

**Status (2026-11-09):** proposed; do not merge. Asked for by the owner on
2026-11-09 and written by the project's agent on Claude Fable 5.2. It is a
menu for the owner to pick from. It changes no code, no decision and no
hardware claim. Reviewed at `b0816d9` (Merge #79).

**How it was made.** The agent read AGENTS.md, the README, every plan under
`.claude/plans/`, the decision log, and the architecture, operations,
hardware, testing and security documents. Three read-only passes read the code
of every domain, and the agent then checked the findings it cites here against
the source. `make verify` and `dev check .` ran on the Arch PC (results in
[section 5](#5-test-and-verify-health-b)). Nothing ran on the board. A finding
marked *read, not reproduced* comes from reading the code and has no failing
test behind it yet.

**Grades** run from A (strong) to F (absent). They grade the project against
its own plans, not against a commercial product.

## In short

1. **The owner cannot use the panel today.** Everything that works on the
   board is a demo copied to `/tmp`, started by hand from the PC, and gone at
   the next power cycle. Four applications are built and host-tested
   (vocabulary, voice-lab recorder, music, bug reports); none is installed.
2. **The queue is the owner's, not the code's.** No hardware gate is closed
   (18 unmeasured values since 2026-10-04), and about a dozen owner steps sit
   in ten different documents. The project keeps building new features ahead
   of that queue instead of shortening it.
3. **The software that runs on the board is the simulator.** The documented
   production UI path can only fail dark. The real one is the simulator's
   `PanelSimulation` class behind a `--simulation-drm` switch, with no
   automatic restart and several ways to exit on bad input.
4. **Engineering discipline is the project's strength.** Evidence is kept
   apart from assumption, secrets handling is careful, `make verify` and CI
   are green, and the neighbouring services' contracts are pinned by digest.
5. **The front page is two platforms out of date.** The README still says
   "BeagleBone House Panel", lists 43 scenarios where there are 65, says the
   enclosure has no CAD, and does not mention music, voice-lab or bug reports.

The five ideas ranked highest in [section 12](#12-ranking):

1. A named **bench runtime**: the panel boots into the UI on the vendor
   image, so it is useful every day while the project image waits.
2. One **owner queue** page, and one sitting that clears most of it.
3. **Harden for unattended use**: fix the crash paths, restart the UI, and
   stop the supervisor from exiting on recoverable faults.
4. **Promote the runtime and unblock the image**: make the simulator class
   the production application, and let the image build without the PIR.
5. **Close the voice loop with the neighbours**: voice-lab speech for spoken
   replies and word pronunciation, and PC-side speech-to-text for touch-started
   "Lexi" commands.

## 1. Where the project stands against its plans

| Plan | State | Evidence |
|---|---|---|
| End-to-end roadmap, 16 steps | Steps 0–2 checked; 13 open. Steps 5–7 (agenda) and 10 (BeagleBone) describe work nobody is doing | `.claude/plans/2026-08-05-end-to-end-development-roadmap.md:168-623` |
| FRDM hardware baseline | 0 of 10 steps checked; ARRIVAL-01 to -07 of 25 done, -13 in part | `.claude/plans/2026-09-25-frdm-hardware-baseline.md`; `docs/operations/frdm-bringup.md:286-326` |
| Pre-board completion | 8 of 16; Steps 7 and 9–15 open | `.claude/plans/2026-09-25-pre-board-completion.md:84-137` |
| Vocabulary on the board (#21) | 5 of 6; the on-board run against live Lexinet waits for tailnet enrolment | `.claude/plans/2026-10-18-vocabulary-study-on-the-board.md:5-10,198` |
| Voice-take capture (#22) | 5 of 7; the formal audio gate and a touch-started take on the board are open | `.claude/plans/2026-10-19-voice-take-capture.md:112,253` |
| Voice-lab recorder (#23) | 8 of 9; the on-board step waits for the owner | `.claude/plans/2026-10-19-voice-lab-recorder-wire.md:291` |
| Music (Spotify Connect) | Built and host-tested; never run against Spotify; waits for the owner's app and sign-in | `music_control/README.md:62,97-98`; `docs/operations/spotify-owner-setup.md:5` |
| Bug reports | Steps 1–4 built, the receiver installed on the PC; step 5 (updates) not started; the reporter is not launched by anything | `docs/architecture/bug-reports-and-self-repair.md:5-33`; `scripts/deploy/frdm_panel_demo_board.py:225,246` |
| Weather and sun times | Proposed on 2026-10-18; 0 of 7; waits for five owner answers | `.claude/plans/2026-10-18-daily-weather-and-sun-times.md:5,87` |
| Mirror unit and enclosure | Fit-check CAD done and built in CI; printing, measuring, the gate board and the speaker are open | `.claude/plans/2026-10-20-mirror-unit-fit-check-enclosure.md:160-164`; `.claude/plans/2026-10-11-mirror-unit-packaging.md:250-287` |
| Personal agenda | Fixture-only screens; no step taken since August | `.claude/plans/2026-08-05-personal-agenda-feasibility.md` (0 of 7) |

**Done and solid:** the supervisor's policy and lifecycle protocol, the
neutral shell with 65 golden scenarios, the study client with its durable
outbox, the capture broker for the microphone array, contract pinning for
Lexinet and voice-lab, the factory backup and the restore procedure, the
flicker fix (ADR 0011, checked on the panel), and the PIR's pins from the
schematic.

**Half-built:** every networked application (built, not installed); the
project image (defined, cannot build for the active board); the reporter's
return path (no update mechanism); spoken commands (a catalog and an exact-match
fake, no recognizer); playback (a filter in front of `aplay`, no service).

**Not started although planned:** PIR debounce and wake-rate policy
(`panel_supervisor/README.md:44-58`), suspend on the FRDM, Wi-Fi, the power
measurement, production update and rollback.

## 2. What blocks value today (grade D)

**The panel does nothing unless a PC session starts it.**
- The only path onto the board is `scripts/deploy/frdm_panel_demo.sh`, which
  copies a bundle to `/tmp/bhp-demo` on NXP's vendor image and stops weston
  (`scripts/deploy/frdm_panel_demo.sh:16-22`). A reboot removes it.
- It is three commands with `--execute`, and it launches only the UI, the
  supervisor, a simulated PIR, the broker and the uploader. Nothing launches
  `bhp-music-control` or `bhp-reporter`.
- The demo runs with suspend disabled and a fixed polar-day location, so the
  panel never turns red at night (`docs/operations/frdm-bringup.md`, "Panel UI
  demo").

**The project image cannot be built for the board.** Two deliberate stops:
- the NXP firmware licence is an open human decision
  (`config/boards/frdm-imx8mp.env:51`; `post-image.sh` refuses without it);
- the device-tree package refuses to render while the PIR values are
  unmeasured (`system_image/README.md:23-32`), and both defconfigs select it.

The first stop is the owner's to decide. The second ties every image
build to one sensor, which is a design choice the project can revisit
([idea B2](#b2-let-the-image-build-without-the-pir)).

**The owner's queue.** These are the open owner steps the agent found, with
where each is written down. Nothing gathers them in one place.

| Owner step | Unblocks | Written in |
|---|---|---|
| Enrol the board in the tailnet with its one tag | Live vocabulary study, voice-lab upload, bug reports, the Lexinet release evidence | `.claude/plans/2026-10-18-vocabulary-study-on-the-board.md` Step 5–6 |
| Decide the NXP firmware licence | Any project image, Wi-Fi on it | `docs/operations/frdm-bringup.md`, ARRIVAL-15 |
| Meter J18 pin 12, then wire the PIR | The PIR gate, the project device tree, suspend and wake | same, ARRIVAL-08 to -11 |
| A supply that boots the board at J2, with an inline meter | The power gate | same, "Arrival status" |
| Run the display, touch and backlight gate script with its trials | Three gates that the 2026-10-18 observations nearly close | same, ARRIVAL-19/20 |
| Create the Spotify app and sign in | Music | `docs/operations/spotify-owner-setup.md` |
| Sign in to voice-lab from the panel; measure a board profile | Keeping takes | `.claude/plans/2026-10-19-voice-lab-recorder-wire.md` Step 9 |
| Answer five weather questions (defaults are recommended) | The weather bar | `.claude/plans/2026-10-18-daily-weather-and-sun-times.md:87` |
| Check the templates, take the caliper list, print | The enclosure | `.claude/plans/2026-10-20-mirror-unit-fit-check-enclosure.md` Steps 5–6 |
| Buy the speaker; build the gate board | The mirror unit | `docs/hardware/mirror-unit-speaker-and-gate.md:5` |
| Install whisper.cpp on the PC | Spoken bug descriptions the agent can read | `docs/operations/bug-report-inbox-setup.md` section 6 |
| Reserve the board's DHCP address | A stable ssh grant | `dev.toml` `[hardware] notes` |
| `AGENDA-011` and the agenda feasibility review | Any agenda work | `README.md`, "Decisions intentionally still open" |

## 3. Architecture (design B+, as running C)

**What is good.** The ownership rules in AGENTS.md hold in the code: one
display and power authority, one microphone owner, credentials held by a
single small process each (ADR 0012, 0013), touch provenance checked where a
capability is used (ADR 0005). Expected failures return structured codes.

**The running UI is the simulator, and the documents say otherwise.**
- `bhp-panel-ui`'s documented backend returns `Failed` for every frame
  (`panel_ui/src/fail_dark_lifecycle_display_commit_backend.cpp:8-13`).
- The UI that runs on the board is entered through `--simulation-drm`
  (`panel_ui/src/panel_ui_main.cpp:368-370`) and is the simulator's
  `PanelSimulation` (`panel_ui/src/simulation_drm_main.cpp:6`). `panel_ui`
  therefore depends on `simulator/`, the reverse of the documented layering.
- `panel_ui/README.md:5-7` says the domain never owns capture or external
  submission; `simulation_drm_main.cpp:62-76` wires the study client, the
  broker, the uploader, the reporter and music into that process's start-up.
- Fixture values ship with it: a constant session and challenge, eight seeded
  cards and an evidence id of "simulation-fixture-not-measured"
  (`simulator/src/panel_simulation.cpp:105,132,138-143`).

**Two files carry the product and are hard to review.**
`simulator/src/panel_simulation.cpp` is 724 lines with lines up to 1,446
characters; `panel_ui/src/simulation_drm_main.cpp` is 165 lines with lines up
to 651. They are one class and one function, written as dense one-liners,
unlike the earlier supervisor code.

**Ways the UI process exits** (*read, not reproduced*):
- Rendering throws on a glyph outside the Latin subset or on text that
  overflows its box (`panel_ui/src/lvgl_panel_display.cpp:35,225-228`). Live
  card text reaches the scene unchecked
  (`simulator/src/panel_simulation.cpp:237`), while music text is guarded
  (`:525`). The contract allows a definition of 16,384 code points of any
  Unicode (`docs/interfaces/lexinet-contract.md:154-156`). One such card ends
  the UI, and the durable session probably restores the same card at restart.
- A malformed control datagram ends the process
  (`simulation_drm_main.cpp:46,145,162-164`).
- If the reporter or music service is down at start-up, attaching throws
  (`:75-76`). If either is lost later, the UI drops it and never reconnects
  (`panel_simulation.cpp:580,680`).

**Ways the supervisor exits.** A backward clock step
(`panel_supervisor/src/panel_policy.cpp:47-58`) and a refused suspend
(`panel_supervisor/src/capture_closure.cpp:356-362`) are both fatal after
start-up. Each is a defensible safety choice on its own. Together with a
restart limit of three (`docs/operations/frdm-bringup.md`, "Services") they
mean a recoverable fault can leave the panel dark until someone intervenes.

**Touch needs a quiet moment.** Every supervisor poll publishes a snapshot
and invalidates touch authority
(`panel_supervisor/src/touch_observation_runtime.cpp:48-54`), so a tap that
uses a capability must start and be claimed between two polls
(`panel_supervisor/README.md:316-318` says so). At the demo's 1 s poll this
is rarely noticed; at 250 ms it may be.

**Release identity couples the UI and the supervisor.** They must carry a
byte-identical build stamp (`panel_ui/src/unix_socket_lifecycle_client.cpp:360-365`).
A partial update leaves the panel dark, which matters once updates exist.

**Duplication across the newer daemons.** Four copies of the socket bind
helper, three near-identical service loops, two token stores, three outboxes
with different failure rules, four SHA-256 helpers and a `result.h` per
domain. Issue #23 asked for a shared outbox "once there is a second user";
there are now three.

## 4. Defects found (*read, not reproduced*)

These are for the sessions that own the code; this review changes none of it.
The bug-report items may overlap the receiver fixes already in progress.

| # | Where | What happens |
|---|---|---|
| 1 | `bug_reporter/src/ingest.cpp:23,38-42`, called from `tools/reporter_main.cpp:237` | A log line whose `level`, `reportable` or `location` is present but not a string (for example `"reportable": true`) throws an uncaught type error and ends `bhp-reporter`. Any service's stderr reaches this path |
| 2 | `bug_reporter/src/reporter_service.cpp:242-257` | The uploader always retries the first queued bundle and ignores whether the failure can be retried. One refused bundle blocks every later report |
| 3 | `bug_reporter/src/reporter_service.cpp:310-318`; `report_inbox/bhp_report_inbox/store.py:169,178` | Repeat counts add on the server, so a lost response double-counts. A pending count above 10,000 is refused with a 400 and retried forever, and blocks the counts behind it |
| 4 | `report_inbox/bhp_report_inbox/store.py:247` | Retention parses the agent-written `updated_at` with `strptime` and nothing catches the error, so one malformed status file stops retention for every report |
| 5 | `voice_lab_recorder/src/outbox.cpp:22-27,85,138` | Nothing removes a take package after it is kept or refused, and the limit counts only unfinished takes. A 600 s six-channel take is about 115 MB, so storage fills |
| 6 | `panel_ui/src/lvgl_panel_display.cpp:35,225-228` | The card-text exit described in section 3 |
| 7 | `panel_supervisor/src/media_window_endpoint.cpp:60-63`; `media_broker/daemon/media_brokerd.cpp:76` | No stale-socket recovery. After a kill, the bind fails; the supervisor logs a warning and runs without the media window |
| 8 | `vocabulary_panel/src/study_synchronizer.cpp:50,94` | No backoff: a failing sync repeats every second while the surface is open, with up to three inner attempts and a log line each |
| 9 | `voice_lab_recorder/tools/uploader_main.cpp:199,256-257` | `--ca-file` in the production binary replaces the trust store for both the issuer and the API with no loopback restriction; the Lexinet client restricts the same hook (`vocabulary_panel/src/http_lexinet_client.cpp:82-87`) |
| 10 | `docs/operations/build-and-deploy.md:27` | The documented cross-build command exports the BeagleBone's 32-bit prefix; `scripts/build/cross_build_applications.sh:44-48` rejects it for the active board |
| 11 | `scripts/verification/linux-x86_64.Dockerfile` | The image installs neither libpng nor ALSA headers, which the host preset now requires, so `make verify-linux-x86_64` should fail at configure (not run) |

Smaller: `json.value(key, default)` throws on a wrong type in several server
answer paths (`voice_lab_recorder/src/outbox.cpp:234`,
`uploader_service.cpp:291`); the music service never re-reads a new token
file without a restart (`music_control/src/music_service.cpp:53,89-93`); the
three service loops make blocking HTTP calls on the thread that serves the UI
socket (`bug_reporter/src/inbox_client.h:34` allows 30 s).

## 5. Test and verify health (B+)

**Run for this review (Arch PC, 2026-11-09):** `make verify` and `dev check .`
both passed; the PR body records the output and what the host skipped.

**Strengths.**
- One command, mirrored in CI, green on every recent run in 13–17 minutes.
- 65 golden scenarios at zero tolerance; process-death tests for the study
  store; the reporter is tested against the real Python receiver.
- The fault matrix is checked for traceability
  (`scripts/verification/check_fault_matrix.py`).

**Gaps.**
- **Skips are quiet.** A missing cross compiler, sysroot, kernel tree,
  Buildroot archive, enclosure venv or `busybox` each skips a step and exits 0.
  The closing line reports only tool versions
  (`scripts/verification/verify_repository.sh:112-119`). The `busybox` skip
  prints nothing at all (`:28-31`).
- **CI never cross-compiles.** The workflow installs no aarch64 compiler and
  sets no kernel or Buildroot path (`.github/workflows/ci.yml:39-42`), so a
  green run has built nothing for the board's architecture.
- **Upstream drift is invisible.** Default verify compares checked-in
  fixtures with checked-in digests. Only the manual `--check` exporters and
  the two live targets (`Makefile:72,83`) can see Lexinet or voice-lab change.
  Spotify and Pocket ID are not pinned at all.
- **The board runtime has no host test.** `simulation_drm_main` and the DRM
  display are exercised only on the board or in the Mac-only VM tier
  (`scripts/sim/virtual_panel_vm.py:85`), which the Arch PC cannot run.
- **No fuzzing** of the hand-written codecs; the mode codec has six test
  cases for 1,007 lines (`shared/panel_mode_protocol/src/codec.cpp`).
- **No caching** in CI, and each PR branch runs twice (push and
  pull_request).
- **`make verify-board` passes when it observes nothing.** With no backup
  directory every storage check is `unobserved` and the exit is still 0
  (`scripts/deploy/frdm_board_health.sh:126-131,178`); failed units and
  temperature are recorded but never judged.

## 6. Hardware evidence (discipline A−, progress D)

**Discipline.** The arrival log is the best document in the repository. It
separates observation from gate evidence, records its own mistake (the boot
partition's access dates, 2026-10-10) and gives a rollback for the one
environment write.

**Progress.** Nine gates, none closed. The display, touch and backlight were
all seen working on 2026-10-18 but as console observations; the gate script
has not rerun with the panel wired (`docs/operations/frdm-bringup.md:317-320`).
Two open questions there affect daily use: touch reports 800×480 on a
1024×600 panel and nobody has checked that taps land where touched, and the
board runs from the PC's USB-C, below NXP's stated input.

**Kernel split.** Evidence comes from the vendor's 6.18 kernel while the
project pins 6.12 (`docs/operations/frdm-bringup.md`, "For the owner to
note"). Every vendor-image observation has to be repeated on the project
kernel, and the gap grows with each month on the vendor image.

**Security of the bench board.** The serial console gives root with no
password, and a root login has been open on it since 2026-10-04. That is
acceptable on a desk; it should be closed before refresh tokens are placed on
the board.

## 7. Documentation drift (C)

The recent documents (bring-up log, bug reports, music, hardware) are
accurate and dated. The entry points are not.

| Where | Says | Is |
|---|---|---|
| `README.md:1`, `CMakeLists.txt:3`, every script's service name, `system_image/.../board/beaglebone-house-panel/` | BeagleBone House Panel | An FRDM i.MX 8M Plus project since ADR 0008 (84 files in the tooling alone still use the name) |
| `README.md:32` | 43 scenarios | 65 (`simulator/scenarios/`) |
| `README.md:36` | Enclosure: "Requirements and measurement checklist"; remaining "CAD" | Parametric CAD exists and CI uploads printable parts (`.github/workflows/ci.yml:55-62`) |
| `README.md` status table and repository map | No row for music, voice-lab, bug reports or the report inbox; `media_broker` is "fake-only" | Four domains added since; the broker has real ALSA capture |
| `README.md:67-68` | Display receipt and inventory pending | Panel lit and touched on 2026-10-18 |
| `.claude/plans/2026-10-11-mirror-unit-packaging.md:5` | "The display has not yet shown a frame" and the array is not ordered | Both happened in October |
| `media_broker/README.md:1,60,84-85` | "Fake Media Broker", `voice_take` only, opens no device | Also serves `bug_report`; `src/alsa_capture_source.cpp:53` opens one |
| `simulator/README.md:204-205` | The default catalog has two surfaces | Four are enabled (`panel_ui/config/applications.json`) |
| `panel_supervisor/README.md:86-88` | No display-commit backend exists | `panel_ui/src/drm_page_flip_display.cpp` |
| `presence_sensor/README.md:3-5` against `:44-46` | "No target compilation" | "compiles against linux-imx lf-6.12.49-2.2.0 for arm64" |
| `docs/operations/build-and-deploy.md:45-46` | `debian@board-name`, `/home/debian` | The FRDM as `root` |
| `docs/operations/development-environment.md:106-110` | A dependency-free check without CMake | The script needs pkg-config and OpenSSL and runs CMake |
| `docs/testing/*.md` | The current validation records | Frozen at 2026-09-25/26 on a Mac; nothing covers voice, music or reports |
| `docs/decisions/README.md` | ADR 0002 "Proposed; hardware-gated" | Buildroot is what the tree builds |
| `docs/architecture/application-capability-ownership.md` | Presence first delivered by the BeagleBone baseline plan | That plan is blocked since 2026-09-24 |

Two structural causes: documents are amended by appending dated notes and
striking old text, so the README is now half history; and status lives in
prose in six places with nothing generated from one source. The volume is
also high: about 21,700 lines of documents and plans against roughly 80,000
lines of hand-written code and tests (generated fonts excluded).

## 8. Developer and owner rough edges (C+)

- **No one-command board run.** Sysroot, build and start are separate
  commands, each needing `--execute` last
  (`scripts/deploy/frdm_panel_demo.sh:31`), and nothing sources
  `config/device.env`.
- **Three aarch64 build paths**: the Buildroot SDK, a GNU cross compiler with
  a verification sysroot, and clang with a Debian sysroot for the demo. Only
  the third produces what runs on the board, and it has no CMake preset.
- **Source lists kept twice.** `scripts/verification/verify_host_cpp.sh:57-290`
  hand-maintains compile lists that CMake also holds.
- **The Arch bootstrap leaves gaps**: no cross compiler, `busybox`, clang or
  Python 3.12 in `scripts/environment/bootstrap_arch_build_host.sh:24`.
- **Mac-only pieces remain** after the move to the Arch PC: the VM tier and
  `scripts/sim/build_local_qemu.sh`.
- **75 remote branches, 72 already merged.**
- **For the owner at the panel:** no way to see what build is running, no
  on-screen sign that a service is down beyond a surface's "unavailable"
  state, and no path from "it broke" to "it restarted" without the PC.

## 9. Security (B+)

- **Good:** no token found in arguments, URLs or logs; refresh tokens are 0600
  and sent only in POST bodies; the inbox admits a peer by tailnet tag before
  the TLS handshake and again per request, and bounds every id, name and size
  (`report_inbox/bhp_report_inbox/server.py:157,276-281`).
- **To tighten:** defect 9 above; the shared HTTP wrapper accepts `http://`
  and leaves the https rule to each caller
  (`shared/http_runtime/src/http_request.cpp:104`); the demo passes tailnet
  names as process arguments (`scripts/deploy/frdm_panel_demo_board.py:246-248`),
  visible in `ps`, which the project's own rule on URLs in arguments covers;
  the inbox has no total quota and its store has no lock under eight threads
  (`store.py:163-173`).
- **A design question:** ADR 0010 kept every credential off the panel. ADR
  0012 and 0013 are two exceptions, and each new service adds a third. See
  [idea B6](#b6-a-panel-gateway-on-the-pc).

## 10. Neighbours

| Neighbour | Today | Gap |
|---|---|---|
| Lexinet | `study.v1` pinned; client built; listener live on the tailnet | The board is not enrolled, so the panel has never studied a live card; only two operations are granted |
| voice-lab (corpus) | `corpus-client.v1` pinned; recorder and uploader built | No sign-in on the board; no measured board profile, so Keep is refused |
| voice-lab (speech) | Not used. voice-lab has a tailnet speech API in the owner's voice, a queued job that returns 24 kHz WAV | The panel has no playback service and is not a registered caller |
| Lexi | In this repository "Lexi" is the spoken-command prefix. There is a catalog and an exact-match fake; no recognizer | The owner's agent server, which voice-lab's speech document names as its intended caller, does not exist yet, so there is nothing to connect to. This review assumes that server is what "the Lexi voice assistant" will be |
| Bug-report inbox (PC) | Receiver installed and hardened (#79) | The panel's reporter is not launched, so the inbox has never received a real report |
| homemesh | No reference in this repository. homemesh is an iPhone app with no server | No natural interface; see idea B10 |

## 11. Ideas

Each idea gives its value, a rough effort (S: a day or less of agent work; M:
several days; L: weeks or new hardware), risks and dependencies. **Owner
decision** marks a choice that is the owner's to make.

### Quick wins

**Q1. One owner queue page.** Move the table in section 2 into a maintained
`docs/OWNER-QUEUE.md`, ordered by what each step unlocks, with the exact
commands. Every plan links to it and stops keeping its own list.
- Value: high. It turns ten documents into one sitting. Effort: S.
- Risk: goes stale unless `make verify-docs` checks that each plan's
  owner-gated step appears in it.

**Q2. Fix the defects in section 4.** Most are a few lines with a test each.
The card-text exit (6) should draw a truncated card with a marker and log a
structured error, never throw.
- Value: high once anything runs unattended. Effort: S–M. Dependencies: none.

**Q3. A verify summary that names its skips.** End `make verify` with one
line listing each skipped step and why, and warn loudly for `busybox`. Add an
aarch64 compiler to CI and cache apt, the CAD venv and LVGL.
- Value: medium; a green run then means what it says. Effort: S.
- Risk: CI time rises with the cross build unless cached.

**Q4. A weekly drift check in CI.** A scheduled job runs the contract
exporters' `--check` and both live contract targets against the neighbours'
current `main`, and opens an issue here when a pin is behind.
- Value: medium; drift is found in a week, not on the board. Effort: S–M.
- Dependencies: the live targets need Go, PostgreSQL 16 and Python 3.12 on
  the runner.

**Q5. Rewrite the README from one status file.** Keep a small
`docs/status.json` (gates, applications, what runs where) and generate the
status table from it; move the dated amendments to a changelog. Fix the rows
in section 7 in the same change.
- Value: medium; the front page stops misleading. Effort: S–M.

**Q6. One command for the board.** `make board-demo` sources
`config/device.env`, builds what changed, starts the demo with all six
services and prints what is running.
- Value: medium for every board session. Effort: S.

**Q7. Restart and reconnect.** The demo's board script restarts the UI when
it exits, and the UI retries a lost reporter or music socket with backoff.
- Value: high for unattended use. Effort: S. Pairs with Q2.

**Q8. An About screen.** A long press on the clock shows the build, each
service's state, the last sync, the queue depths and the board's address.
- Value: medium; the owner can read the panel's health at the panel.
  Effort: S–M (one surface and goldens).

**Q9. Accept the weather plan's defaults.** The plan has waited three weeks
for five answers and recommends a default for each.
- Value: medium; the Home bar becomes live. Effort: M to build.
- **Owner decision:** the five answers, in particular sending the panel's
  public address to a geolocation service.

**Q10. Fuzz the codecs.** Short libFuzzer runs in CI for the mode codec, the
touch codec, the study wire codec and the reporter's `classify`.
- Value: medium; defect 1 is the kind a fuzzer finds in seconds. Effort: S–M.

**Q11. Retire the BeagleBone name.** One mechanical rename of the project,
service names and the Buildroot board directory.
- Value: low to medium; removes a standing confusion. Effort: M (wide diff).
- **Owner decision:** the name. Risk: churn against open branches.

**Q12. Prune merged branches** (72 of 75 remote). Effort: S.
- **Owner decision:** deleting branches is hard to undo, so it needs a go-ahead.

### Bigger bets

#### B1. A bench runtime the owner can use every day

Name a third tier between simulation and production: the vendor image
running the project's services from persistent storage, started at boot,
with weston off. The panel then shows the clock, live vocabulary, music
control and the Report button every day, and feeds real bug reports to the
inbox.
- Value: the highest in this document. It ends "cannot use it", and the
  bug-report loop starts receiving real reports.
- Effort: M. systemd units, an install directory, a config directory outside
  Git, real day and night from the configured location, the UI restart of Q7.
- Risks: it writes the board's storage and enrols the tailnet, both
  owner-gated. It runs the vendor's 6.18 kernel, not the project's 6.12. No
  suspend and no PIR, so presence comes from touch and a schedule. Tokens on a
  board whose console gives root must wait for the console to be closed.
- Dependencies: tailnet enrolment; Q2 and Q7 first.
- **Owner decision:** an ADR that accepts this tier, says its records are
  not gate evidence, and says what may be installed on the vendor image.

#### B2. Let the image build without the PIR

Split the device tree so the bring-up image builds with the PIR node absent,
and the PIR overlay is added when its values are measured. The firmware
licence then becomes the only stop before a first project-image boot.
- Value: high; the project kernel boots weeks earlier, and every later gate
  is measured on the kernel that ships.
- Effort: M. Risks: weakens a deliberate guard, so the production defconfig
  should keep refusing. Dependencies: the licence decision; a flashing
  session with the owner.
- **Owner decision:** whether a PIR-less bring-up image is allowed.

#### B3. Make the running UI the production application

Move `PanelSimulation` out of `simulator/` into a production application
module with a real supervisor connection, split it by surface, reformat it,
and delete the fail-dark default once the display gate closes. The simulator
then wraps the application; it no longer is the application.
- Value: high; the code that runs is the code that is documented, reviewed
  and tested. Effort: L. It should follow issue #23's "one app descriptor".
- Risks: a large move under open feature branches; goldens must stay
  byte-identical through it.
- **Owner decision:** when the display counts as proven enough to drop
  fail-dark.

#### B4. One daemon skeleton and one outbox

Extract the socket bind with stale-socket recovery, the service loop with
HTTP off the socket thread, the token store and the durable outbox into
`shared/`. Each domain keeps what it sends and its credentials, as the
ownership rule requires.
- Value: medium to high; removes defects 2, 7 and 8 as a class and makes the
  fifth service cheap. Effort: M–L. Risk: a refactor across four working
  daemons with no feature to show for it.

#### B5. Signed A/B updates (the accepted design's step 5)

Already accepted in the bug-report design: a read-only root, a data
partition, two slots, automatic rollback and a held tap to install.
- Value: high in the long run; fixes reach the panel without a flashing
  session. Effort: L. Dependencies: B2 and a booting project image.
- A cheaper first stage for B1's tier: a signed bundle of binaries that the
  bench runtime verifies and swaps, with the previous bundle kept for
  rollback. **Owner decision:** whether that interim stage is wanted.

#### B6. A panel gateway on the PC

The report inbox is already a tailnet service on the PC that admits the
panel by tag. Extend that pattern to one small gateway that fronts third
parties for the panel: weather, a calendar's "today", music control and a
fleet status feed. The panel then holds no third-party token.
- Value: medium to high; new tiles need no new ADR exception, and the PC can
  cache, redact and rate-limit. Effort: L, and the gateway is infra's.
- Risks: the panel depends on the PC being on; music control stops when it
  is off. It reverses part of ADR 0013.
- **Owner decision:** tokens on the panel (today) or on the PC.

#### B7. The panel speaks, in the owner's voice

Add a playback service to the broker and register the panel as a voice-lab
speech caller. Because speech is a queued GPU job, the panel asks ahead and
caches: the pronunciation of each study word, short confirmations and a
morning line.
- Value: high for study (hearing the word) and the base for any assistant.
- Effort: M–L. Dependencies: the speaker and its amplifier; voice-lab's
  deploy steps; a caller credential.
- Risks: a third credential on the panel unless B6 fronts it; latency of
  seconds, so nothing conversational without caching; the speaker shares a
  box with the microphone array.
- **Owner decision:** registering the panel as a caller, and what it may say.

#### B8. Touch-started "Lexi" commands through the PC

Keep ADR 0005's rule that a touch starts capture. The broker records a few
seconds under a new purpose and sends it to a speech-to-text service on the
PC (whisper.cpp is already planned there for bug reports); the transcript is
matched against the existing catalog and admitted as a namespaced intent.
- Value: high; real spoken commands without choosing an on-device
  recognizer first, and the recorded takes become the test set for one later.
- Effort: L. Dependencies: B1 or the project image; a PC-side endpoint
  (infra's); the array's processed channel identified.
- Risks: household audio leaves the panel for the PC, which the threat model
  must accept; latency of one to three seconds; nothing works when the PC is
  off.
- **Owner decision:** whether command audio may go to the PC. An on-device
  wake word stays a separate, later decision.

#### B9. The panel as the assistant's room endpoint

When the owner's agent server exists, the panel is its natural first room:
a microphone array, a speaker, a screen and presence. Define a small
`room-endpoint.v1` contract now (ask with audio or text, receive text, a
speech job id and an optional card to show), pinned like `study.v1`, and
build the panel side against a fixture server.
- Value: the largest, and the furthest away. Effort: L. It needs B7 and B8.
- Risks: designing against a server that does not exist; scope.
- **Owner decision:** whether the assistant gets a room endpoint at all, and
  who owns the contract.

#### B10. Capture a term from the mirror

Lexinet has durable capture submissions for later enrichment. A "Capture"
button on the study surface, using the report form's on-screen keyboard or a
spoken take, sends a term to Lexinet, where the owner's agents enrich it.
- Value: medium to high; the panel adds to the lexicon as well as reading it.
- Effort: M here, plus a third operation in `study.v1` and a wider tailnet
  grant on Lexinet's side.
- **Owner decision:** widening the panel's Lexinet capability beyond create
  and sync. The request to Lexinet goes through the owner, since Lexinet is
  not among this project's request targets.

#### B11. A nightly check on the real board

The board is cabled to the PC and the agent may ssh to it. A scheduled
session runs `make verify-board`, starts the demo in `/tmp`, walks five
scenarios, takes framebuffer screenshots, compares them with the goldens
and stops, then files what changed.
- Value: medium to high; the first automated test of the DRM path
  (section 5) and of each merge on real hardware. Effort: M.
- Risks: it stops weston and takes the screen for a few minutes; a failed
  run could leave the panel dark until the next one.
- **Owner decision:** a standing go-ahead for unattended `/tmp`-only runs.

#### B12. Card pre-flight

Check every card against the panel's glyph set and layout before it reaches
the renderer, on the panel (truncate and mark) and as a report the owner can
run over the Lexinet profile to list cards that will not fit.
- Value: medium; removes defect 6's cause as well as its symptom. Effort: M.

#### B13. A small read-only agenda

The agenda design (a signed Mac companion, Messages, dictation) has not
moved since August. A "today" list from one calendar, read through B6's
gateway or a private ICS link, would give most of the daily value.
- Value: medium. Effort: M. Risks: abandons part of an accepted design.
- **Owner decision:** descope birthday wishes and the Mac companion, or keep
  them as the goal.

#### B14. A morning brief from the fleet

Tiles fed by the owner's other services through B6: open agent requests and
failing CI across the fleet, the day's first event, and travel time from
aptscout's transit data.
- Value: medium, and personal. Effort: M per tile once B6 exists.
- **Owner decision:** which projects may publish to the mirror; a bathroom
  mirror is seen by guests.

#### B15. homemesh

The fit is weak: homemesh is a phone app with no server. The one honest link
is measurement. The panel's Wi-Fi gate records signal strength at a known
spot, which homemesh could import as a ground-truth sample for its coverage
model, and homemesh could show the panel as a wired device in the plan.
- Value: low. Effort: S here; the import is homemesh's to accept.
- Recommendation: leave it until the panel is on Wi-Fi.

### Where newer models could help

- **Fixing from bundles.** The accepted design already has an agent read a
  report bundle and open a fix. B1 is what gives it reports to read.
- **Reviewing pixels.** A PR that changes goldens gets a contact sheet of
  before and after, and a model review of each changed image for clipped
  text, overlap and contrast in night-red mode. Effort: S–M.
- **Wiring checks from photos.** The agent already transcribed the board's
  marking from the owner's photos. Before a power-on, the owner photographs
  the jumpers and the agent checks them against the pin table. It is advice,
  not evidence; the meter still decides.
- **Evidence to pull request.** When a gate script's record lands in
  `hardware_tests/results/`, the agent drafts the profile change that cites
  it, for the owner to review. Effort: S.
- **A synthetic command corpus.** Use voice-lab's speech to render every
  catalog phrase and its confusable neighbours, play them through the
  speaker to the array, and score candidate recognizers before the owner
  records anything. This turns the confusability analysis in
  `docs/architecture/spoken-command-distinctiveness.md` into a measurement.
  Effort: M; needs B7's playback.
- **Keeping documents true.** A scheduled session rereads the README and the
  status documents against the code and opens one docs PR with the drift,
  as section 7 does by hand.

## 12. Ranking

Ranked by value to the owner per unit of effort, with dependencies first.

| Rank | Idea | Value | Effort | Needs the owner for |
|---|---|---|---|---|
| 1 | B1 bench runtime (with Q7) | Highest | M | An ADR, storage writes, tailnet enrolment |
| 2 | Q1 owner queue page | High | S | Reading it, then one sitting |
| 3 | Q2 fix the defects; harden the supervisor's fatal paths | High | S–M | Nothing |
| 4 | B3 with B2: production runtime and a PIR-less bring-up image | High | L | Two decisions, the licence, a flashing session |
| 5 | B7 with B8: speech out and touch-started commands | High | L | Speaker, caller registration, audio-to-PC decision |
| 6 | B11 nightly check on the board | Medium–high | M | A standing go-ahead |
| 7 | Q3 and Q4 verify summary, cross build and drift check in CI | Medium | S–M | Nothing |
| 8 | Q5 README from one status file | Medium | S–M | Nothing |
| 9 | B4 daemon skeleton and outbox | Medium–high | M–L | Nothing |
| 10 | B10 capture a term | Medium–high | M | A wider Lexinet grant |
| 11 | Q9 weather | Medium | M | Five answers |
| 12 | B12 card pre-flight | Medium | M | Nothing |
| 13 | Q8 About screen; Q6 one command; Q10 fuzzing | Medium | S–M | Nothing |
| 14 | B6 panel gateway | Medium–high | L | An architecture decision; infra work |
| 15 | B5 signed updates | High, later | L | A booting project image |
| 16 | B13 agenda; B14 morning brief | Medium | M each | Scope decisions |
| 17 | B9 room endpoint | Largest, furthest | L | The agent server existing |
| 18 | Q11 rename; Q12 prune branches | Low–medium | S–M | A name; a go-ahead |
| 19 | B15 homemesh | Low | S | Nothing yet |

## 13. Decisions this leaves with the owner

1. Is a bench runtime on the vendor image an accepted tier (B1)?
2. May a bring-up image build without the PIR (B2)?
3. The NXP firmware licence (already open since September).
4. Third-party tokens on the panel, or behind a gateway on the PC (B6)?
5. May command audio leave the panel for the PC (B8)?
6. May the panel be a voice-lab speech caller, and what may it say (B7)?
7. Does the agenda keep its Mac-companion scope (B13)?
8. May the panel's Lexinet capability grow to capture (B10)?
9. A standing go-ahead for unattended `/tmp`-only board runs (B11)?
10. The project's name, and whether merged branches may be deleted.

One recommendation on order: decide 1 first. It costs the least, and every
later idea is easier to judge once the panel has been on the wall for a week.
