<!-- @canonical board-profiles -->

# Board profiles

A board profile is the single source of board facts for build scripts,
Buildroot packages, verification, and on-target evidence scripts. Nothing else
may hard-code a board's architecture, compiler tuple, defconfig, device tree,
backlight, touch, PIR, suspend, audio, or Wi-Fi value.

| Profile | Status | Decision |
|---|---|---|
| [`frdm-imx8mp.env`](frdm-imx8mp.env) | `active` (the default when `BHP_BOARD` is unset) | ADR 0008 |
| [`beaglebone-black.env`](beaglebone-black.env) | `history` (board lost; builds kept evaluating) | ADR 0003, superseded by ADR 0008 |

## Format

- One `BHP_BOARD_<KEY>=<value>` per line; `#` comments and blank lines allowed.
- Values match `[A-Za-z0-9_./:,@+=*%-]*`: no spaces, quotes, `$`, or `#`. The
  same file is read by POSIX sh, included by make, and parsed by Python.
- `BHP_BOARD_SSH_TARGET` and `BHP_BOARD_UPLOAD_DIR` are reserved deployment
  variables and may not appear in a profile.
- Required keys and value patterns: [`board-profile.schema.json`](board-profile.schema.json).

## Placeholders

| Form | Meaning | Consumer behavior |
|---|---|---|
| `UNMEASURED:<gate>` | Needs on-board evidence for `<gate>` (an ADR 0008 `FRDM-GATE-*` row for the active board) | `BOARD_PROFILE.VALUE_UNMEASURED`; renderers exit 3; nothing substitutes a default |
| `HUMAN_DECISION_REQUIRED:<id>` | Needs a recorded human decision, such as `NXP-FIRMWARE-EULA` | `BOARD_PROFILE.HUMAN_DECISION_REQUIRED` |

`EXPECTED_*` keys are source-derived hints that evidence scripts compare
against; they are never runtime configuration.

Replace a placeholder only in a reviewed commit that links the evidence
captured under `hardware_tests/results/` (or the accepted ADR for a human
decision). `docs/operations/frdm-bringup.md` lists which evidence script closes
which key.

## Interfaces

```sh
scripts/boards/show_board_profile.sh                         # JSON: values, placeholders, counts
scripts/boards/show_board_profile.sh --require BHP_BOARD_PIR_GPIO_LINE   # exit 3 while unmeasured
scripts/boards/show_board_profile.sh --board beaglebone-black --value BHP_BOARD_CMAKE_PRESET
python3 scripts/verification/verify_board_profiles.py        # schema, gates, references, negative fixtures
```

Shell consumers source `scripts/boards/board_profile.sh` and use
`bhp_board_profile_load_selected` and `bhp_board_profile_require_into`. Buildroot
selects the profile with `BR2_BHP_BOARD_ID`; `system_image/buildroot_external/external.mk`
includes it, and the image installs it at
`/usr/share/beaglebone-house-panel/board-profile.env`.
