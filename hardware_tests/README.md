# Hardware Tests

Owns reversible discovery, wake, display, and power experiments. Test output belongs under ignored `hardware_tests/results/`; curated conclusions belong in `docs/hardware/` or an ADR.

## Safety order

1. Photograph and inventory unpowered hardware and wiring.
2. Verify PIR datasheet and powered output in isolation before connecting it to the BBB.
3. Boot a verified non-flasher microSD and prove eMMC is unmounted/read-only.
4. Image and hash eMMC before booting or modifying its installed OS.
5. Run read-only discovery.
6. Test display and GPIO while awake.
7. Run suspend only with a local console/recovery path and explicit acknowledgement.
8. Measure power at a documented supply boundary.

## Read-only inventory

On the BeagleBone:

```sh
hardware_tests/scripts/collect_read_only_board_inventory.sh \
  hardware_tests/results/board-inventory-YYYYMMDD
```

Review every captured file before sharing it; network addresses, serial numbers, and logs may be private even when no credentials are collected.

## Suspend/wake trials

The suspend script does nothing without `--execute`:

```sh
sudo hardware_tests/scripts/run_suspend_wake_trials.sh \
  --mode mem \
  --implementation "$IMPLEMENTATION_UNDER_TEST" \
  --starting-level "$OBSERVED_STARTING_LEVEL" \
  --wake-edge "$WAKE_EDGE_UNDER_TEST" \
  --expected-wake-source "$EXPECTED_PLATFORM_WAKE_SOURCE" \
  --expected-interrupt-label "$DISCOVERED_PROC_INTERRUPTS_LABEL" \
  --count 1 \
  --output hardware_tests/results/wake-smoke \
  --execute
```

Every matrix value is explicit. Replace each shell variable with evidence from
the exact implementation and wiring under test; none is a hardware default.
Start with one cycle, a serial console, and a known alternate wake/recovery
method. Increase to the acceptance counts only after failures are understood.

For each attempt, the runner creates a read-only `trial-NNNN/expectations.json`
audit record and a replaceable `observation.json` before suspend, then emits
its correlation ID in a structured `wake_trial_prepared` event. A separate
measurement process records the external stimulus, shared
timing marker, confirmed state entry, platform/M3 wake reason, the companion
control reference, `EV_SW` code 11 event, and post-resume sensor level into that
same correlation-scoped record. It should replace the file atomically before
the runner classifies it. An independent recorder writes the companion result
to `negative_control.json`; the `wake_trial_prepared` event names both paths.
The runner passes every matrix dimension and its capture,
return, and after-capture timestamps directly to the classifier from shell
memory. The classifier rejects either JSON file if it changes correlation ID,
implementation, requested state, selected sleep, starting level, wake edge, or
expected wake/IRQ source. The file mode is defense in depth; the classifier
does not trust the on-disk expectations file as the root of truth. If the
external process is unavailable or late, the generated manifest is honestly
`unobserved`; process continuation and cumulative `wakeup_sources` counters
never become substitute attribution.

Each trial directory contains:

- `raw/before/` and `raw/after/`: suspend statistics, `/proc/interrupts`, the
  selected `mem_sleep` text, runner timestamps, and supporting
  `wakeup_sources` evidence;
- `raw/suspend_write_result`: whether the power-state write returned after
  resume or was rejected;
- `expectations.json`: read-only audit copy of the runner-owned matrix cell;
- `observation.json`: independently captured evidence initialized entirely as
  unknown rather than with plausible values;
- `negative_control.json`: separately captured companion-control evidence; it
  is absent until the independent recorder completes it;
- `wake_trial_manifest.json`: recomputed deltas and the final five-way class.

The canonical classifier applies precedence in this order: `aborted`,
`timeout`, `wrong_source`, `entered_and_attributed`, then `unobserved`. Only
`entered_and_attributed` is success. It requires exactly one successful
suspend-stat increment, no failed-suspend increment, a positive unambiguous
expected IRQ delta, matching wake reason, observed external stimulus, passed
independent negative control, the `SW_FRONT_PROXIMITY` Linux event, and a
resume observation. The expected `/proc/interrupts` label must match one exact
descriptor token; prefixes and substrings are not attribution. The confirmed
state must match the requested state and
selected `mem_sleep`: record `freeze` for a freeze entry or
`mem:<selected-value>` such as `mem:deep` for a mem entry.
For a `mem` trial the runner refuses to suspend unless it can observe the
selected value, and the classifier requires identical before/after raw
snapshots. For `freeze`, `mem_sleep_selection.status` is explicitly
`not_applicable`.
The event value and re-read sensor level must agree with the declared starting
level and wake edge. Success additionally requires this nondecreasing UTC
chain: trial start, runner before-capture, confirmed state entry, external
stimulus, Linux event, runner power-state return, post-resume sensor read, and
classification. A second runner-owned chain requires power-state return,
after-capture, then classification; after-capture and the independently
recorded resume observation may occur concurrently. This rejects stale
pre-trial observations and future-dated after-capture evidence. Missing,
decreasing, ambiguous, or contradictory evidence fails closed to `unobserved`.

The observation record's negative-control `status` is never acceptance
evidence. The classifier separately loads `negative_control.json`, validates
its subject and distinct control correlation IDs against the observation,
matches the evidence source, validates its UTC interval, and derives the final
status from `stimulus_applied` and `unexpected_wake_observed`. A pass requires
both values to be false. The control interval must not overlap the subject: it
either completes before the subject trial starts, or starts after both the
subject's recorded resume and the runner's after-capture and completes before
classification. Missing, malformed, wrong-ID, source-mismatched, overlapping,
or future-dated companion evidence remains `unobserved`; a valid companion
that records a stimulus or unexpected wake is `failed` and makes the subject a
`wrong_source` result.

The normal runner workflow is intentionally two phase. It classifies and
retains every requested attempt immediately, prints one structured summary
with exact counts for all five outcomes, and exits nonzero if any attempt is
not `entered_and_attributed`. Before counting, the summary validates the full
manifest structure and independently recomputes state, sleep-selection, wake
source, correlation-ID, edge, resume, counter, and temporal bindings; a
classification-only or internally contradictory success is invalid. A
post-subject control therefore produces an expected early `unobserved` result.
After the recorder has completed the non-overlapping control and atomically
written `negative_control.json`, replay the pinned classifier arguments to
replace the manifest, then regenerate the aggregate summary:
replay every `--expected-*` value from an independently captured
`wake_trial_classification_expectations` runner log or external run sheet,
never solely from the mutable trial directory. The complete interface is shown
by `classify --help`; `--require-success` exits nonzero for every other class,
including `aborted` and `wrong_source`:

```sh
python3 hardware_tests/scripts/classify_wake_trial.py classify \
  --evidence-directory hardware_tests/results/wake-smoke/trial-0001 \
  --expected-correlation-id "$CORRELATION_ID" \
  --expected-implementation "$IMPLEMENTATION_UNDER_TEST" \
  --expected-requested-state mem \
  --expected-selected-mem-sleep "$OBSERVED_SELECTED_MEM_SLEEP" \
  --expected-starting-level "$OBSERVED_STARTING_LEVEL" \
  --expected-wake-edge "$WAKE_EDGE_UNDER_TEST" \
  --expected-wake-source "$EXPECTED_PLATFORM_WAKE_SOURCE" \
  --expected-interrupt-label "$DISCOVERED_PROC_INTERRUPTS_LABEL" \
  --expected-trial-started-at-utc "$TRIAL_STARTED_AT_UTC" \
  --expected-before-capture-at-utc "$BEFORE_CAPTURE_AT_UTC" \
  --expected-process-returned-at-utc "$PROCESS_RETURNED_AT_UTC" \
  --expected-after-capture-at-utc "$AFTER_CAPTURE_AT_UTC" \
  --require-success

python3 hardware_tests/scripts/classify_wake_trial.py summarize \
  --trials-directory hardware_tests/results/wake-smoke \
  --expected-count "$REQUESTED_TRIAL_COUNT" \
  --classification-error-count 0
```

The structural manifest contract is
[`schemas/wake_trial.schema.json`](schemas/wake_trial.schema.json). Draft
2020-12 cannot express cross-field equality or temporal ordering, so the schema
is not an acceptance oracle. Semantic success requires the canonical
classifier and summary recomputation described above. Fixture tests exercise
all five classes without a board:

```sh
python3 -m unittest hardware_tests.tests.test_classify_wake_trial
```

## Evidence requirements

Each result set names board/cape/sensor revisions, wiring, supply, kernel,
device tree, Git commit, command, time, expected outcome, actual outcome, and
every failure. Wake-trial manifests follow `schemas/wake_trial.schema.json`;
power samples follow `schemas/power_measurement.schema.json`.

The current power schema represents preliminary scalar BeagleBone stable-state
samples only. It cannot encode transition traces, controller/application-off,
media workloads, uncertainty, or cross-platform battery-boundary comparisons.
Do not squeeze those observations into its existing enum. Extend and verify the
schema as Step 7 requires before collecting candidate-comparison evidence.

**Amended 2026-08-07:** replaced the smoke-only wake description with the
correlated manifest, fail-closed classifier, and explicit external-evidence
workflow delivered by review-amendments Step 13. No target trial was run and no
hardware support claim changed.
