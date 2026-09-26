# Plan: Panel Home and Reel Clock

**Goal:** add the requested app launcher, permanent daytime navigation/weather
bar, full dim-red night field and large black clock with upward digit changes.

**Out of scope:** real personal-data providers, household timezone selection,
hardware calibration, production enablement, system-wide installation and pushes.

## Context

The user requested this follow-up after reviewing the completed UI milestone at
`5171b65`. This request supersedes the red-digits-on-black night presentation.
Keep the existing dim red configuration unless the user specifies otherwise.
The launcher's working surfaces are Lexinet and the Agenda preview. Returning
Home must preserve canonical study state and pending reviews while discarding
provisional exposure and Undo as required by the study reducer.

Clock and weather provenance are independent of study data. Advance explicitly
labelled simulation time; do not select the still-gated AGENDA-011 household
timezone path. Forecast location is a pending user question; unavailable data
must not become invented temperatures. Explicit fixtures exercise both units,
temperature-dependent colors and unavailable/stale conditions.

Every animation frame follows the ordinary render-generation and DRM commit
path. The guest holds a lifecycle raster until its acknowledgement has been
transmitted. Local animation commits do not manufacture lifecycle acknowledgements
or touch readiness. Night, blanking, distrust and disconnection cancel input.

## Steps

- [x] **Step 1: Add the bounded clock font**
  - Generate a pinned 192 px digits/colon subset without expanding all Latin
    fonts. Validate coverage, integrity and byte-for-byte regeneration.
  - Verify: `python3 scripts/fonts/generate_panel_fonts.py --check`

- [x] **Step 2: Implement navigation and animated presentation**
  - Extend the canonical intent registry, surface routing and effect graph.
    Preserve durable study state across Home/app transitions.
  - Add shared day chrome, temperature projection, icons and clipped clock
    frames with explicit monotonic sampling. Preserve failure/lifecycle gates.
  - Verify: `make verify`

- [x] **Step 3: Review all tiers and publish visual evidence**
  - Include before/after images with changed goldens. Capture deterministic
    animation samples plus a viewable animation, and leave Mac windows running.
  - Verify: `make verify-panel-goldens`
  - Verify: `make verify-panel-simulator`
  - Verify: `make verify-panel-vm`
  - Verify: `make verify-docs`

## Done criteria

The Home icons route through the shell, daytime chrome stays visible, night is
red with a large black clock, and changed digits move upward across all tiers.
No invented live time/weather or hardware claims. Appropriate regressions and
all repository gates pass; local commits and visual artifacts are available.

**Amended 2026-09-26:** created for the user's explicit follow-up request. The
prior pre-board UI milestone remains complete; broader Phases 2–7 remain separate.

**Completed 2026-09-26:** all steps verified. Host and Linux suites each pass
44 CTests; all 43 scenarios pass across headless/native/guest tiers. Guest Home
navigation and 10 intermediate DRM animation frames are recorded in
[validation](../../docs/testing/home-reel-ui-validation.md). Native and QEMU
previews remain running on Home with explicitly simulated time/weather.
