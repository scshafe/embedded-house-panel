# Golden display baselines

Initial baseline: **2026-09-25**. Before: no shared LVGL golden image suite existed.
After: the same 29 scenarios render through `panel_ui::LvglPanelDisplay` and its
subordinate vocabulary renderer at 1024×600. These are synthetic fixtures.

![Initial after contact sheet](review/initial-after.png)

`manifest.json` fixes the scenario inventory, dimensions and exact pixel budget
(channel tolerance 0; changed pixels 0). `make verify` rerenders and compares it.
On mismatch, `.build/screenshots/` receives the actual, pink-highlighted diff,
and structured report. No display, animation timing or platform fonts are used.

To change a baseline intentionally, run `make update-panel-goldens`, inspect the
before/after files under `.build/screenshots/review/`, and commit review images
with the changed goldens and a reason. Updates are staged only after every
scenario renders successfully. Never relax tolerance to conceal a layout change.

**Amended 2026-09-25:** established the initial reviewed baseline. No image is
physical display or FRDM evidence.
