# Panel fonts

Owns the project font recipe, generated LVGL bitmap assets, and the
`@canonical panel_font_glyph_coverage` manifest check shared by the neutral shell
and subordinate surfaces. It has no dependency on either application domain.
The renderer first uses the shell's canonical UTF-8 policy to enforce validity,
controls and budgets, then calls `validate_glyph_coverage`. Missing glyphs fail
explicitly; no platform font, replacement box or fallback is selected.

`panel_fonts/panel_fonts.h` exposes C symbols `bhp_font_24`, `bhp_font_28`,
`bhp_font_36`, `bhp_font_40` and `bhp_font_48` through `bhp::panel_fonts`.
The C++ `panel_fonts/glyph_coverage.hpp` API is independently available through
`bhp::panel_font_coverage`; it needs neither LVGL nor a display. A failed check
identifies the first scalar and byte offset without retaining personal text.

Coverage is printable Basic Latin, Latin-1 Supplement, Latin Extended-A,
en/em dashes, curly quotes, bullet, ellipsis, prime/double prime, euro and
trademark. LF is a layout control. This is **not full Unicode coverage**:
CJK, emoji and decomposed combining sequences fail; precomposed supported
characters such as `é` render. The renderer does not normalize user content.

The source Montserrat Medium TTF is copied verbatim from the
[official LVGL v9.5.0 font sources](https://github.com/lvgl/lvgl/tree/v9.5.0/scripts/built_in_font).
Its OFL-1.1 license has only trailing ASCII spaces and tabs stripped; all words
and line breaks are preserved. `glyph_manifest.json` records both upstream and
stored-license SHA256 pins and that normalization, alongside exact source,
upstream archive and converter-lock pins. Generated subsets retain OFL-1.1; installation
includes its copyright and license under `share/bhp/licenses/panel_fonts/`.
All tiers compile identical committed C assets with 4-bit antialiasing.

Normal builds need no Node, npm, generator or font download. Host verification
checks asset hashes and coverage. LVGL tests additionally resolve every
manifest glyph in every size, reject blank substitutes and check unsupported
scripts fail. The generator has JSON output and a dry-run default.

To regenerate after an intentional recipe change, install the locked developer
tool only in the ignored build directory (no system installation):

```sh
mkdir -p .build/panel-font-generator
cp scripts/fonts/package.json scripts/fonts/package-lock.json .build/panel-font-generator/
npm ci --prefix .build/panel-font-generator --ignore-scripts --no-audit --no-fund
python3 scripts/fonts/generate_panel_fonts.py --execute
python3 scripts/fonts/generate_panel_fonts.py --check-reproducible
python3 scripts/fonts/generate_panel_fonts.py --check
```

The converter is [LVGL's official lv_font_conv](https://github.com/lvgl/lv_font_conv),
pinned at 1.5.3 including its bundled rasterizer/dependencies through npm
integrity. The wrapper replaces its machine-specific command-line header and
normalizes the file ending to exactly one newline; font tables and bitmap bytes
must regenerate identically. No visual change is
accepted solely because generation succeeds: approved renderer changes still
require headless, native and guest screenshots.
