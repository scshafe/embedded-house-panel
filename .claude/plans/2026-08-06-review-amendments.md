# Plan: Apply the 2026-08-06 Review Amendments

**Goal:** close the defects and contradictions that the adversarial architecture review found, so that roadmap Step 2 can begin against a corrected, self-consistent contract set.

**Out of scope:** approving any pending product decision; changing the status of `LEXI-001`–`LEXI-007`, `VOICE-001`, or `AGENDA-001`–`AGENDA-010`; marking roadmap Step 1 complete; implementing `panel_ui`, protocol v2, the media broker, or the Lexinet study API; buying hardware; any change that requires the BeagleBone.

Two amendment buckets from the review are deliberately **not** steps here, because they are gated on evidence this plan cannot produce. They are recorded in the register at the bottom so they are not lost:

- **Defer until hardware is available:** wireless implementation, trial-count reconciliation, power-schema versioning, tzdata packaging, touch calibration, Buildroot LTS-vs-stable switch, Tailscale version override.
- **Future-product concerns:** always-on MCU registry rows and ADR, read-only root plus bounded writable partition, OTA/update-and-rollback ADR, encrypted or hardware-bound storage, display burn-in, ambient brightness, bystander authentication.

## Context

- Source of these amendments: the 2026-08-06 adversarial review of `3aa12b0`. Every item below traces to a finding whose evidence was read directly in this repository, in `/Users/coleshaffer/lexinet`, or in a current primary source.
- The review changed nothing. This repository is unmodified at the time of writing.
- **This plan is written to be reviewed before it is executed.** Roadmap Steps 6 and 11 already establish that convention (`.claude/plans/2026-08-05-end-to-end-development-roadmap.md:293`, `:387`); it applies here too.
- Step 1 is a human decision gate. Steps 5, 7, and 8 cannot be executed correctly until it is answered, because each encodes a choice that is the user's to make, not the executor's.
- When amending an existing plan or document, follow the repository convention: strikethrough the obsolete text with `~~...~~`, insert the replacement, and append `**Amended 2026-08-06:** <reason>` at the bottom. Never silently rewrite — the change must be traceable in `git diff`.
- Any ADR created here allocates its four-digit prefix by scanning `docs/decisions/` at creation and re-scanning immediately before merge (`docs/decisions/README.md:22-32`). `0004` is currently free; that is not a reservation.
- `make verify` requires `ripgrep` and `shellcheck` on the host (`scripts/environment/check_host_dependencies.sh:47`). Both are installed on the development host as of 2026-08-06, and `make verify` passes there with exit 0 — only the `KERNEL_DIR` and `BHP_BUILDROOT_DIR` checks skip, by design. A verify failure on this host is therefore a real regression, not an environment gap. The suite still does not preflight its declared dependencies, so a host missing one reports a documentation failure rather than a dependency error; Step 12 fixes that.
- `make verify-release-candidate` cannot run on this host: it requires Linux x86_64 and aborts with `RELEASE_CANDIDATE.HOST_UNSUPPORTED` (`.build/release-gate-host-check.log`). No Buildroot image has ever been built end to end, which is why the Step 3 defects went undetected.

## Steps

- [x] **Step 1: Record the human decisions that later steps depend on**
  - Capture the user's answer to each of the following in a new `docs/decisions/` record or an explicit section of the roadmap — whichever the user prefers — with the date and the decision text. Do not infer an answer from silence, and do not proceed past a blank.
    1. Is a UI presentation-acknowledgement timeout a *fault* (daemon dies) or a *degraded non-fatal state*? (Blocks Step 5.)
    2. May `panel_ui` ever send the supervisor anything other than a presentation acknowledgement? (Blocks Step 7 and Step 8.)
    3. What object is `appliance_release_id` — the image, the application bundle, a pair of binary hashes, or is the field deleted from the binding? (Blocks Step 7.)
    4. Accept the reduced protocol-v2 binding set, or keep all eight fields? (Blocks Step 7.)
    5. Will anything other than the panel ever submit Lexinet study events? (Recorded here; consumed by a later Lexinet plan amendment, not by this plan.)
    6. Grace period: dim or dark, and what duration? (Blocks Step 9.)
  - Notes: these are decisions 2, 3, 4, 5, 6, and 13 from the review's decision slate. The remaining slate items (bystander authority, battery requirement, wireless approach, Buildroot line, Linux build host, PIR part, `mac_companion/` location, voice-qualification scale) are not prerequisites for any step in this plan and stay open.
  - Verify: human review — the executor stops here and does not continue until every answer above is written down.

- [x] **Step 2: Correct the protocol-v1 consumer claim before it misinforms a review**
  - `docs/interfaces/panel-mode-protocol.md:8` states that `vocabulary_panel` consumes version 1, and `docs/architecture/process-interconnectivity.md:16` diagrams that socket as implemented scaffold. Neither is true: no `AF_UNIX`, `connect(`, `display_mode`, or `protocol_version` reference exists anywhere under `vocabulary_panel/`.
  - Change both to state that version 1 is published but unconsumed, and rewrite the migration section (`docs/interfaces/panel-mode-protocol.md:15-68`) to describe a greenfield protocol replacement rather than a compatibility-window risk. Keep the atomic-cutover conclusion; it is correct. Delete the `supported_protocol_versions: [1]` error envelope at `:130-145`, which describes a UI→supervisor direction that version 1 does not have.
  - Verify: `make verify-docs` passes, and `rg -n 'AF_UNIX|protocol_version' vocabulary_panel/` still returns nothing, confirming the corrected claim.

- [x] **Step 3: Fix the two Buildroot defects that would stop the first image build**
  - Add `BR2_TARGET_ROOTFS_EXT2_SIZE` to both `system_image/buildroot_external/configs/beaglebone_house_panel_defconfig` and `..._bringup_defconfig`. Upstream's default is `"60M"` (Buildroot 2026.05.1 `fs/ext2/Config.in`), which cannot hold the selected package set — the official Tailscale ARM tarball alone is ~34.7 MB compressed. Choose a value at or below the 512 MB rootfs partition that upstream `genimage.cfg` declares.
  - Add a project-owned `genimage.cfg` under `system_image/buildroot_external/board/beaglebone-house-panel/` naming only `am335x-boneblack.dtb`, and point `BR2_ROOTFS_POST_SCRIPT_ARGS` at it. Upstream `board/beagleboard/beaglebone/genimage.cfg` names eight DTBs; the defconfig builds one (`beaglebone_house_panel_defconfig:23`).
  - Add both new values to the assertion list in `scripts/verification/verify_buildroot_external_tree.sh:92-99` so a regression fails without a full build.
  - Notes: neither defect has ever surfaced because the image has never been built — `.build/release-gate-host-check.log` ends in `RELEASE_CANDIDATE.HOST_UNSUPPORTED`, and no `.img` or `rootfs.ext4` exists in the tree.
  - Verify: `BHP_BUILDROOT_DIR=<pinned 2026.05.1 tree> scripts/verification/verify_buildroot_external_tree.sh` passes with the new assertions; `make verify` passes.

- [x] **Step 4: Reorder the PIR driver so the input device is registered before its IRQ**
  - `presence_sensor/experiments/custom_pir_input_driver/beagle_house_pir_input_driver.c:161` requests the threaded IRQ before `input_register_device` at `:169`. `input_event_dispose()` writes `dev->vals[dev->num_vals++]` with no NULL guard, and `vals` is allocated only inside `input_register_device()`, so an edge arriving in that window dereferences NULL. A PIR asserted or chattering during its warm-up makes this a plausible boot-path oops.
  - Either register the input device first and then request the IRQ — the initial-state sync at `:174` already covers edges missed in the gap — or request with `IRQF_NO_AUTOEN` and call `enable_irq()` after registration. Preserve the existing `devm` cleanup ordering and the hard-fail-on-wake-setup behavior.
  - Verify: `KERNEL_DIR=<kernel build tree> scripts/verification/verify_presence_sensor_sources.sh` compiles the module cleanly; add a source-order assertion to that script so the ordering cannot silently regress.

- [ ] **Step 5: Give the supervisor a presentation outcome that is not a cycle fault**
  - Today the blank path applies the backlight, calls `notify_ui_mode`, and returns early on failure (`panel_supervisor/src/panel_supervisor.cpp:311-329`), so the suspend request at `:331` is unreachable. The daemon then treats the failed cycle as fatal (`panel_supervisor/daemon/panel_supervisord.cpp:358-363`). Protocol v2 turns timeout, disconnect, stale, and duplicate acknowledgements into normal events on this exact path.
  - Split the seam into `publish(snapshot) -> Result<PendingPresentation>` and `await_presentation(deadline) -> Committed | NotCommitted | Failed`. Only `Committed` may gate nonzero brightness; none of the three outcomes may abort the cycle or skip the suspend request. Reclassify presentation failures out of `fail_cycle` entirely.
  - Add tests proving that a failed, timed-out, and disconnected presentation each still reach the suspend request with brightness at zero.
  - Notes: this is the amendment that makes the code honor the roadmap's own non-negotiable — "UI failure may prevent illumination but must never prevent blanking or suspend" (`.claude/plans/2026-08-05-end-to-end-development-roadmap.md:192`). Requires the answer to Step 1 decision 1.
  - Verify: `scripts/verification/verify_host_cpp.sh panel_supervisor` passes with the new cases; `make verify` passes.

- [ ] **Step 6: Add restart supervision for the supervisor daemon**
  - The fail-dark exit path is only safe if something restarts the process. `S70panel_supervisord` provides `start|stop|restart|status` only, and BusyBox `rcS` runs it once — no respawn exists in the external tree.
  - Add restart supervision with an explicit retry limit and backoff, and structured logging of each restart, without weakening the blank-before-exit behavior at `panel_supervisord.cpp:233` and `:366`.
  - Verify: `scripts/verification/verify_build_and_deploy_contracts.sh` passes with a new assertion covering the restart configuration; `make verify` passes.

- [ ] **Step 7: Settle and freeze the protocol-v2 field set**
  - Using the Step 1 answers to decisions 2, 3, and 4, rewrite the version-2 binding table (`docs/interfaces/panel-mode-protocol.md:33-53`) to the agreed field set. If the reduced set is chosen: keep `connection_challenge`, `snapshot_id`, `display_mode`, `render_generation`, `presentation_result`, and the `SO_PEERCRED` UID check; demote `supervisor_session_id` and `event_sequence` to inputs of `snapshot_id`, echoed for diagnostics only. If `appliance_release_id` is retained, name its producer; if not, delete it from the binding and state where release atomicity is enforced instead.
  - Enumerate the complete version-2 `display_mode` set in the same document. `day_active` is currently required by `.claude/plans/2026-08-05-end-to-end-development-roadmap.md:194`, `README.md:102`, and `docs/architecture/lexinet-study-integration.md`, but is not a legal value at `docs/interfaces/panel-mode-protocol.md:93`.
  - Record the answer to decision 2 in the document as a durable invariant, so Step 8 and any future media-lease work inherit it.
  - Specialist: `rigorous-architect`
  - Verify: human review confirms the frozen field set matches the recorded decisions; `make verify-docs` passes.

- [ ] **Step 8: Move voice effect-admission enforcement to the capability holders**
  - The admission matrix (`docs/architecture/spoken-command-distinctiveness.md:100-111`) is enforced by the catalog validator and re-checked inside `panel_ui` — the process that `docs/security/threat-model.md:65` and `docs/architecture/personal-agenda-and-birthday-wishes.md:708` both concede can fabricate touch-level requests. The three capability holders never verify provenance: `docs/architecture/personal-agenda-and-birthday-wishes.md:496` and `:517-521` show lease and confirmation requests with no touch-vs-voice discriminator.
  - Amend the architecture documents so that (a) the media broker refuses `capture_start` unless the lease was opened by a supervisor-observed touch, (b) the personal-context client refuses dispatch unless the confirmation token carries a `touch` provenance field set by whoever observed the touch, and (c) an acceptance gate inspects the reachable call graph per `intent_id` rather than the catalog alone. Keep the matrix itself unchanged — it is correct.
  - Resolve the media-lease authority contradiction in the same pass: `docs/architecture/personal-agenda-and-birthday-wishes.md:496` has `panel_ui` requesting the lease from the supervisor, `docs/architecture/application-capability-ownership.md:37` assigns leases to the broker, `:696` says the broker must not join the UI socket group, and `docs/interfaces/panel-mode-protocol.md:107` declares the UI→supervisor direction command-free. Pick one and make the other three match.
  - Because this moves a trust boundary, allocate an ADR using the collision-safe procedure in `docs/decisions/README.md:22-32`.
  - Specialist: `doddy`
  - Verify: human review confirms every capability holder validates provenance independently of `panel_ui`; `make verify-docs` passes with the new ADR registered.

- [ ] **Step 9: Reconcile the stale behavior documents against code and roadmap**
  - `docs/behavior/panel-policy.md` describes fault behavior three different ways — retain output (`:16`), retain last value (`:40`), terminate (`:66`) — and the code terminates. It also routes features inside `PanelPolicy` (`:21`, `:36`), which `.claude/plans/2026-08-05-end-to-end-development-roadmap.md:195` forbids. Correct both, and record the Step 1 decision-6 grace-period answer here as the single source of truth, superseding the conflicting `dark_idle` description at `docs/architecture/power-domain-architecture.md:98`.
  - `docs/behavior/flash-card-ux.md:36` and `:98` forbid offline rating, while `docs/architecture/lexinet-study-integration.md:519-527` queues ratings through the outbox. The outbox model is the intended one; mark the UX document's progression superseded, as its own header at `:3-8` already anticipates.
  - Fix the incidental drift found alongside: `docs/research/gui-strategies.md:62` states Linux 6.18.38 where the defconfig pins 6.18.21; `README.md:125-134` omits `shared/` and `config/` from the repository map; the Lexinet plan says HTTP where the code enforces HTTPS.
  - Verify: `make verify-docs` and `make verify` pass; a search for the grace-period behavior returns one authoritative statement.

- [ ] **Step 10: Split the two subordinate plan steps the roadmap executes in halves**
  - Roadmap Step 2 executes "the platform-spine portion of Lexinet Step 7" (`.claude/plans/2026-08-05-end-to-end-development-roadmap.md:182`) and Step 4 executes the rest (`:233`), but Lexinet Step 7 is a single checkbox with a single Verify block (`.claude/plans/2026-08-05-lexinet-study-integration.md:400`, `:451-462`). After roadmap Step 2 lands, the shell and protocol will exist while that checkbox still reads unchecked with them as outputs — inviting a later session to rebuild them as peers.
  - Split Lexinet Step 7 into 7a (spine: shell, protocol v2, intent envelope, packaging) and 7b (vocabulary surface), and Step 1 into 1a (review) and 1b (contract authoring), each with its own Verify clause. Update the roadmap to invoke the sub-steps by name.
  - Add a gate-to-step mapping table to the roadmap; the dependency map uses `S0`–`S4`/`H0`–`H7` (`:82-112`, `:477-492`) while the body uses `Step 0`–`15` with no correspondence stated.
  - Verify: `make verify-docs` passes; every roadmap step that invokes a subordinate plan names a step that exists in that plan.

- [ ] **Step 11: Give the unowned requirements an owner or an explicit deferral**
  - **Wireless.** `.claude/plans/2026-08-05-end-to-end-development-roadmap.md:394` requires proving asynchronous Wi-Fi/Tailnet recovery at Step 12, but the production defconfig has only `BR2_SYSTEM_DHCP="eth0"`, `system_image/README.md:48` states no Wi-Fi profile is included, and `docs/operations/debian-tailnet-bringup.md:433` defers Wi-Fi entirely. Add a row to the work-partition table (`:123-136`) recording the chosen approach, or amend the Step 12 criterion to match a wired prototype. Do not leave an acceptance criterion with no provisioning path.
  - **Timezone data.** `docs/architecture/personal-agenda-and-birthday-wishes.md:406-408` flags that Buildroot omits tzdata and offers two options; no defconfig entry, plan step, or decision ID owns the choice. Assign one. (Solar policy is UTC plus coordinates and correctly needs no tzdata; this is an agenda and clock-display concern.)
  - **`shared/http_runtime/`.** `docs/architecture/application-capability-ownership.md:34` names Lexinet plan Step 6 as first delivery authority; that plan never mentions it, while `vocabulary_panel/src/http_lexinet_client.cpp:71` and `:75` hold `curl_global_init`/`cleanup` inside a feature. Add a sub-step to Lexinet Step 6 with its own Output and Verify lines, forbidding `curl_global_*` outside the shared runtime.
  - **Unbuilt registry paths.** Six paths cited in the registry at `:38-39` do not exist. Mark each row's status explicitly rather than relying only on the general disclaimer at `:21-22`.
  - Verify: `make verify-docs` passes; every requirement in the review's traceability matrix has a named owner or a written deferral.

- [ ] **Step 12: Make the host verification suite reach the code it claims to cover**
  - Stand up a Linux x86_64 runner (a container is sufficient) that executes `scripts/verification/verify_host_cpp.sh all`. On macOS every `__linux__` path is dead code, so the daemon poll loop, suspend backoff, signal handling, and the `EVIOCGSW` read path (`panel_supervisor/src/evdev_presence_source.cpp:147-160`) have no executed test anywhere.
  - Add a daemon smoke test that runs `panel_supervisord` against temporary-directory sysfs fixtures and a fake evdev source for several cycles, asserting the structured-log trajectory rather than only the exit code.
  - Make the LVGL renderer and HTTP client reachable on the host: add a `host-dev` preset turning `BHP_BUILD_HTTP_LEXINET_CLIENT` and `BHP_BUILD_LVGL_RENDERER` on (`CMakeLists.txt:12`, `CMakePresets.json:17`), give the HTTP adapter its first tests (TLS verification, redirect refusal, size limits), and include both in `verify_host_cpp.sh`. Correct the `README.md:26` work-stream row, which currently reads as though the HTTP client is host-tested.
  - Assert the suspend-critical kernel symbols — `CONFIG_AMX3_PM`, `CONFIG_WKUP_M3_IPC`, `CONFIG_WKUP_M3_RPROC`, `CONFIG_TI_EMIF_SRAM` — in the Buildroot Kconfig check. They are modules in `omap2plus_defconfig`, and `bhp-presence-sensor.mk:26-35` enables none of them; TI's current SDK documentation requires the whole chain for `mem` to reach DeepSleep0.
  - Have `verify_repository.sh` run `scripts/environment/check_host_dependencies.sh` first and record tool versions in the pass log, so a missing `ripgrep` reports a dependency error rather than a documentation failure.
  - Verify: `make verify` passes on the Linux runner with the daemon smoke and new adapter tests included; the Kconfig assertions fail when any of the four symbols is disabled.

- [ ] **Step 13: Build the evidence tooling the hardware gates already assume**
  - Add a wake-trial manifest schema alongside `hardware_tests/schemas/power_measurement.schema.json`, requiring per-trial correlation ID, external stimulus record, confirmed state entry, suspend-stat delta, interrupt delta, wake reason, negative control, and the five-way classification the hardware plan demands (`.claude/plans/2026-08-05-beaglebone-hardware-baseline.md:101-104`). Add a classifier testable against canned sysfs fixtures. `hardware_tests/scripts/run_suspend_wake_trials.sh` currently records timestamps and `wakeup_sources` only, and `hardware_tests/README.md:36-44` says so honestly — but no step before the board returns builds the replacement.
  - Extend `collect_read_only_board_inventory.sh` to capture `/proc/interrupts`, `/sys/power/mem_sleep`, `/sys/power/suspend_stats`, pinctrl state, and cape EEPROM — the baselines the attribution deltas need.
  - Add `schema_version` to the power schema.
  - Add a per-plan-step verification map (clause item → test symbol → command) and a check that fails when a step's Verify clause has unmapped items. Lexinet Step 4's Verify is roughly twenty behaviors in one clause (`.claude/plans/2026-08-05-lexinet-study-integration.md:270-289`) and Step 6's is roughly thirty (`:381-397`); neither is auditable by a human as written.
  - Add the cross-artifact `SW_FRONT_PROXIMITY` contract check asserting the identical event code in the driver, both overlays, and `evdev_presence_source.cpp`.
  - Specialist: `testineer`
  - Verify: `make verify` passes with the new checks; the classifier correctly rejects an aborted and a wrong-source trial fixture.

- [ ] **Step 14: Amend the threat model for the principals it does not currently have**
  - Add "any person with room access" as a first-class principal. `docs/security/threat-model.md:33` models physical access as theft only, while touch is the human-intent proof for both capture start and irreversible submission (`docs/architecture/personal-agenda-and-birthday-wishes.md:464`). Record the user's decision as either an accepted limitation in the asset table or a requirement for a factor a bystander lacks. Do not resolve it silently.
  - Add an adversarial-acoustic row covering crafted audio and inaudible-injection classes, and decide whether a voice-originated `presented` fact — currently voice-admitted at `docs/architecture/spoken-command-distinctiveness.md:105` and uncorrectable, unlike ratings — should be local-only until a touch or rating promotes it.
  - Re-scope the physical-access boundary now that the asset behind it includes the companion signing key: `docs/security/threat-model.md:160-162` places sustained physical access out of scope, but the loss is no longer a vocabulary panel.
  - Specify that the `wish_recording` stop detector emits only a boolean and timestamp, shares a purpose-scoped arena wiped at quiesce, and runs with core dumps disabled; and that quiesce is verified by the supervisor rather than self-reported by the broker.
  - Add PIR wake-rate limiting and debounce to the supervisor's responsibilities: `beagle_house_pir_input_driver.c:57-59` calls `pm_wakeup_event` on every edge with no ceiling, and `docs/architecture/power-domain-architecture.md:50-55` already names wake-rate backoff for the future MCU.
  - Specialist: `doddy`
  - Verify: human review confirms each new principal has either a control or a written accepted limitation; `make verify-docs` passes.

- [ ] **Step 15: Reframe Lexinet Step 5 as remediation of a live exposure**
  - `requireBearer` returns the handler unchanged when the token is empty (`/Users/coleshaffer/lexinet/internal/server/server.go:155-158`), every mutation route sits inside that group (`:53-58`, `internal/server/api.go:89-98`), the shipped deploy config sets `auth_token = ""` (`deploy/docker/config.toml:18`), and `docker-compose.yml` documents that tailnet exposure is bridged by a TCP-mode `tailscale serve`, which carries no identity and applies no authentication.
  - Amend `docs/architecture/lexinet-study-integration.md:665` and Lexinet plan Step 5 to state that the study work must *close* an existing unauthenticated path, not merely add a scoped one, and that the Step 5 negative test ("a nontagged node cannot reach or authorize to the study service") does not currently hold.
  - Notes: the Lexinet repository is a separate checkout and this plan does not change it. The interim mitigations — setting a token, and making an empty token with the tailnet listener enabled a startup error rather than an auth bypass — belong to that repository and to the user. Record them here as prerequisites, not as executed work.
  - Verify: human review confirms the panel-side documents no longer assume a configured bearer, and that the interim mitigation is either done or explicitly accepted as an open risk with a date.

## Done criteria

- Steps 1–15 are checked, or an unchecked step carries a written reason and an owner.
- `make verify` passes on the development host and on the Linux runner introduced in Step 12.
- No pending product decision has changed status, and roadmap Step 1 remains unapproved.
- Each amended plan or document carries its `**Amended 2026-08-06:**` footer, and the amendments are visible in `git diff`.
- The deferred register below is intact, with nothing quietly dropped.
- The work is committed on a branch and left for review; this plan does not authorize a merge.

## Deferred register

Carried forward from the review; each needs evidence or a decision this plan cannot supply.

**Defer until hardware is available:** wireless implementation once the approach from Step 11 is chosen; reconciling the four divergent suspend-trial populations (20 / 50-per-cell / 100-per-cell-plus-1,000-soak / unnumbered) across `docs/research/gui-strategies.md`, `.claude/plans/2026-08-05-beaglebone-hardware-baseline.md:111`, `:144`, `docs/research/compute-platform-assessment.md:323-325`, and roadmap `:488`; extending the power schema for transition traces and battery boundaries; tzdata packaging; touch calibration; decomposing `FlashCardStage` and retyping the renderer on a neutral frame — do this *with* the shell, not after; Buildroot stable-versus-LTS (2026.05.1 reaches end of life in September 2026, LTS 2025.02.16 runs to March 2028); the Tailscale external-tree version override, needed before the first Buildroot tailnet test and before the agenda design's Serve app-capability dependency on 1.92 can be satisfied against the pinned 1.78.1; the kernel point-release refresh policy (6.18.21 pinned, 6.18.43 current).

**Future-product concerns:** always-on MCU registry rows and its ADR; read-only root with a bounded writable partition; an OTA update-and-rollback ADR, which must land before enclosure CAD; encrypted or hardware-bound storage if the companion signing key remains on the panel; display burn-in policy for a near-static surface; daytime ambient brightness; bystander authentication if Step 14 does not accept the limitation.

**Simplification candidates raised by the review, not adopted here** — each would reduce scope but is a product decision: dropping gap-free ingestion ordinals, the client-stream receipt ledger, and the request-hash conflict path for a single-writer reality; dropping the predecessor flush-receipt barrier in favor of the server-side exhaustion predicate; deferring content-addressed recognition-policy artifacts while keeping the effect-admission matrix; scaling the voice qualification protocol down from roughly a thousand utterances and 24 hours of negative audio; cutting `scripts/verification/verify_documentation.sh` from 437 lines of phrase-count assertions to link validity, secret absence, and placeholder detection.

**Amended 2026-08-06:** recorded the verified host baseline in Context. `ripgrep` was installed after this plan was written, so `make verify` now passes on the development host with exit 0; a failure there is a regression rather than a missing tool. Step 12's preflight item is unchanged — it makes the suite report a missing dependency as a dependency error on the next host, including the Linux runner that same step introduces. Also recorded that `make verify-release-candidate` cannot run here, which is why the Step 3 Buildroot defects were never surfaced by a build.
