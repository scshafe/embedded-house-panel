# Hardware Assumptions Register

Every assumption remains provisional until linked evidence exists under `hardware_tests/results/`.

**Current target (2026-09-24):** FRDM i.MX 8M Plus + Waveshare 7inch DSI LCD (C),
per [ADR 0008](../decisions/0008-adopt-frdm-imx8mplus-and-waveshare-7-inch-dsi-development-platform.md).
The BeagleBone Black was lost; its rows are retained as history and are not active targets.

The machine-readable survey of every BeagleBone/AM335x assumption in code,
configuration, and scripts, with its `retarget` / `board_parameterize` /
`bbb_history` classification and disposition, is
[`frdm-retarget-inventory.json`](frdm-retarget-inventory.json).

## FRDM i.MX 8M Plus assembly (active)

| Assumption | Source | Risk | Evidence required |
|---|---|---|---|
| Board is FRDM-IMX8MPLUS with 4 GB LPDDR4, 32 GB eMMC, onboard IW612 | NXP product page; UM12340 | A distributor listing describes 2 GB / 16 GB / M.2 Wi-Fi | Board markings, `/proc/meminfo`, eMMC size, Wi-Fi driver probe |
| Board input is 12–20 V USB-C PD, at least 45 W supply | UM12340 | Undersized or non-PD supply fails to boot or browns out under load | Board-input voltage/current trace across states |
| Display is Waveshare 7inch DSI LCD (C), 1024×600 on J15 (22-pin, 0.5 mm); its I2C, 5 V and GND by separate wires to J27 | UM12340 §4.1, Figures 6 and 7; NXP overlay `imx8mp-frdm-waveshare-7inch-c-panel.dtso` (bridge and touch on i2c3) | Wrong cable variant or overlay mismatch; I2C wires missing, so the driver binds and DRM shows `DPI-1` connected while the panel stays dark (seen 2026-10-11; with the J27 wires on 2026-10-18 it lit) | DRM connector/mode listing, the bridge answering at i2c3 `0x45`, visible test pattern |
| Touch is Goodix GT911 on `i2c3` at `0x14` | NXP overlay | Extra I2C wiring required; no interrupt defined | `i2cdetect`, stable evdev node, touch event capture |
| Backlight is software-controllable to 0 (off) | Waveshare wiki (Raspberry Pi) | NXP overlay has no backlight node | `/sys/class/backlight` node, `max_brightness`, measured backlight-off current |
| A header GPIO can wake suspend-to-RAM from a PIR | i.MX 8M Plus capability; Toradex Verdin measurements | FRDM carrier, BSP, or Wi-Fi driver may block suspend or wake | Repeated attributed wake per the assessment observation contract |

## BeagleBone Black assembly (inactive; board lost)

| Assumption | Legacy clue | Risk | Evidence required |
|---|---|---|---|
| Board is BeagleBone Black with 4 GB eMMC | Saved device tree and developer recollection | Different board/revision changes boot, pins, and PM | Markings, EEPROM, block layout |
| BeagleBone Black should be the final product platform | It is already owned and legacy code targets AM335x | External Wi-Fi and limited inference could distort the cordless product around sunk hardware | Complete Black baseline plus common i.MX/BeagleY/QCS power, wake, media, and workload comparison |
| Display is a 4D 480×272 cape | Legacy GTK window and backlight path | Model/revision/touch stack unknown | Cape markings/EEPROM, live DT, DRM/fb/input discovery |
| Backlight exposes 0–100 | Legacy code and 4D documentation | Node name/range and “off” behavior vary | `max_brightness`, measured current, visual cycle test |
| PIR has one 3.3 V-safe state output | Typical PIR behavior | Legacy code used two pins; voltage may be unsafe | Exact part datasheet and isolated powered measurement |
| P9_21/GPIO0_3 is usable | Legacy overlay | Cape may consume pin; mux/wake configuration may differ | Pin conflict audit and live GPIO ownership |
| GPIO0 can wake chosen DeepSleep0 | AM335x documentation | Kernel/PM firmware/cape may prevent it | Repeated wake test with wake-source evidence |
| Tailnet returns promptly after resume | Product intent | Interface/DHCP/Tailscale latency may be user-visible | Timed network and peer-restoration measurements |

The official 4D brightness interface and AM335x wake documentation are
summarized in the research documents; neither replaces measurement on this
assembly. The [compute platform assessment](../research/compute-platform-assessment.md)
keeps future-board marketing claims subject to the same rule.
