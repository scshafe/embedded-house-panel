# Home launcher and reel-clock validation

This follow-up to `5171b65` implements the user's requested Home launcher,
daytime menu bar and dim-red night screen. It is simulation evidence only.
The implementation plan is
[Panel Home and Reel Clock](../../.claude/plans/2026-09-26-panel-home-and-reel-clock.md).
Machine-readable status and verification counts are in
[home-reel-ui-validation.json](home-reel-ui-validation.json).

## Presentation and behavior

- Home launches Lexinet and the existing Agenda preview through canonical shell
  intents. Leaving study retains canonical session/history/outbox while releasing
  provisional voice exposure and Undo through the existing reducer.
- Every daytime app has a Home control at the top left, a centered clock, and
  High/Low values at the top right. Forecast colors depend on the temperature,
  independently of whether the value is the daily high or low.
- Night fills the screen with the configured dim red (`#180000` by default) and
  a large black 192 px digits/colon font. It has no menu or app hit targets.
- Both clocks move only changed digits upward over 400 ms. The colon and unchanged
  digits stay still. Every frame uses an ordinary render generation and, in the
  guest, the existing DRM commit path. Lifecycle changes cancel active motion.
- A lifecycle raster is held until the canonical client has sent its complete
  acknowledgement. Failed acknowledgement completion allows private fixture
  recovery; it does not enable touch. Actual DRM commit failure remains terminal.

## Data provenance

The advancing clock is explicitly a simulation clock, seeded by the selected
scenario. It does not select the still-gated household timezone/civil-time path.
Weather examples are marked Preview; default Home shows unavailable values.
Available/stale examples exercise Fahrenheit, Celsius and temperature color bands.
No live forecast provider/location has been configured. Agenda remains a preview.
The user's existing brightness configuration and supervisor authority are retained;
no physical brightness or readability measurement is implied.

## Review artifacts

The committed [scenario manifest](../../panel_ui/tests/golden/manifest.json)
contains 43 states, including five frozen samples of each clock animation and
Home weather variants. The [before/after review](../../panel_ui/tests/golden/review/home-reel-before-after.png)
and [review metadata](../../panel_ui/tests/golden/review/home-reel-before-after.json)
retain the prior presentation alongside the requested changes.

Local review products are `output/pdf/house-panel-home-clock-screens.pdf`
(43 bookmarked pages), `.build/home-reel/review/house-panel-four-screen-overview.png`,
and `.build/home-reel/motion/{day,night}-reel.gif`. The GIFs use actual shared-renderer
frames, with holds before/after the 400 ms transition. PNG baselines remain the
pixel-exact comparison source. PDF rendering/visual-QA evidence is in
`.build/home-reel/review/artifact-validation.json`.

## Verification scope

The current status record distinguishes repository checks, 44 host CTests,
43 exact headless/native images, real Linux guest input/presentation tests,
and the final 43-scenario guest comparison. Guest grace and untrusted-clock states
retain their existing declared semantic checks. No comparison tolerances are relaxed.

Source review found and resolved the initially static daytime clock and the
private-control recovery gate after a failed presentation. Visual review covered
Home, stale weather, study back, Agenda, both mid-reel states and the night screen;
there was no unintended clipping or overlap.

Physical FRDM tests, production personal-data providers, household timezone and
live weather configuration remain open. The prior validation record is historical;
its red-digits-on-black night presentation is superseded by this follow-up.
