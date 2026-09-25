<!-- @canonical development-platform-selection -->

# ADR 0008: Adopt FRDM i.MX 8M Plus and Waveshare 7-inch DSI as the Development Platform

**Status:** accepted; purchase decided, hardware not yet received or measured

**Date:** 2026-09-24

## Context

[ADR 0003](0003-characterize-beaglebone-before-selecting-product-platform.md)
kept the BeagleBone Black as the characterization target because it was
already owned and cost nothing. The Black has since been lost, so that reason
no longer holds. Buying another Black would pay to characterize a platform the
[compute platform assessment](../research/compute-platform-assessment.md)
already rejects for the product: it has no onboard Wi-Fi and cannot credibly
run local short-utterance STT or gesture inference.

The assessment names the NXP i.MX 8M Plus as the best-balanced product
baseline. It has published suspend-to-RAM evidence (Toradex Verdin, about
0.10–0.18 W in named configurations), published local STT timings from NXP,
onboard Wi-Fi on the evaluation board, audio input, and an NPU.

## Decision

| Role | Item | Identifier | Purchase requirement |
|---|---|---|---|
| Compute | NXP FRDM i.MX 8M Plus development board | `FRDM-IMX8MPLUS` | 4 GB LPDDR4, 32 GB eMMC, onboard IW612 Wi-Fi 6/BT 5.4/802.15.4 |
| Display | Waveshare 7inch DSI LCD (C) | Waveshare SKU `20429` | 1024×600 IPS, 5-point capacitive touch (Goodix GT911 over I2C) |
| Display cable | 22-pin DSI FPC cable (Raspberry Pi 5 variant) | — | Must match the board's J15 22-pin 0.5 mm DSI connector; confirm it is in the display box |
| Board power | USB-C Power Delivery charger | — | 12–20 V PD; at least 45 W, 65 W preferred (UM12340) |

- The FRDM board replaces the Black as the **development and
  characterization platform**.
- This ADR does **not** select the final product hardware. A successful
  prototype is expected to move to an i.MX 8M Plus system-on-module (Verdin,
  DART, or similar) on a purpose-built carrier. The final product ADR still
  requires measured evidence.
- Start bring-up on NXP's supported Yocto image, using the upstream NXP kernel
  overlay `imx8mp-frdm-waveshare-7inch-c-panel.dtso` (`nxp-imx/linux-imx`,
  branch `lf-6.12.y`). Port the Buildroot appliance image only after display,
  touch, backlight, suspend, wake, and Wi-Fi work on the vendor image. Buildroot
  2026.05.1 has `imx8mp-evk_defconfig` and `freescale_imx8mpevk_defconfig` as
  starting points. It has no FRDM-specific defconfig.

## Procurement snapshot (2026-09-24)

Prices and stock change; re-check before buying.

| Seller | Unit price (USD) | Stock observed |
|---|---|---|
| [NXP](https://www.nxp.com/design/design-center/development-boards-and-designs/FRDM-IMX8MPLUS) | 250.00 | Pending stock |
| [DigiKey](https://www.digikey.com/en/products/detail/nxp-usa-inc/FRDM-IMX8MPLUS/26918033) | not captured | Listed as "ships today" |
| [Newark](https://www.newark.com/nxp/frdm-imx8mplus/development-board-arm-cortex-a53/dp/48AM3465) | 275.48 | 1 in stock (UK), 64 incoming |
| [Mouser](https://www.mouser.com/ProductDetail/NXP-Semiconductors/FRDM-IMX8MPLUS?qs=CoAU%2FpHVZXyJS2Vr5xMP8Q%3D%3D) | 240.08 | 0; 71 expected 2026-10-06; possible 38% US tariff |
| [Arrow](https://www.arrow.com/en/products/frdm-imx8mplus/nxp-semiconductors.html) | not captured | Out of stock |
| [Waveshare (display)](https://www.waveshare.com/7inch-dsi-lcd-c.htm) | 62.99 | Available |

Mouser's listing text describes 2 GB RAM, 16 GB eMMC, and Wi-Fi through M.2.
That conflicts with NXP's 4 GB / 32 GB / onboard IW612 specification. Verify
the variant before paying.

## Known gaps and hardware gates

| ID | Gap | Evidence required |
|---|---|---|
| `FRDM-GATE-BACKLIGHT` | NXP's Waveshare overlay defines the panel and touch but no backlight node. On Raspberry Pi the panel exposes `/sys/class/backlight/*/brightness` 0–255 with 0 = off. | `/sys/class/backlight` node present on the FRDM image, or a device-tree/driver addition that provides it; brightness 0 measured as backlight off |
| `FRDM-GATE-TOUCH` | GT911 on `i2c3` at `0x14`; the board manual (UM12340) requires separate I2C wiring from display to board. The overlay defines no interrupt line. | Exact wiring, stable evdev node, polling vs interrupt behavior, touch provenance under ADR 0005 |
| `FRDM-GATE-POWER` | Board input is 12–20 V USB-C PD at 45 W or more. The proposed cordless power domain assumed a 5 V-class application rail. | Measured board-input power in every stable state; revised power-domain design (PD source/boost or wall power) |
| `FRDM-GATE-SUSPEND` | Toradex suspend numbers are for Verdin modules, not the FRDM carrier. | Suspend-to-RAM entry, attributed GPIO/PIR wake, and board-input power on the FRDM board |
| `FRDM-GATE-WIFI` | IW612 driver must survive suspend or be cleanly quiesced. | Repeated resume with Wi-Fi, tailnet, and Lexinet recovery timings |
| `FRDM-GATE-PIR` | No PIR pin assignment exists for this board. The Black's P9_21/GPIO0_3 assumption no longer applies. | Header pin selection, 3.3 V level check, wake-capable GPIO proof |
| `FRDM-GATE-IDENTITY` | A distributor listing describes a 2 GB / 16 GB / M.2 Wi-Fi variant that conflicts with NXP's 4 GB / 32 GB / onboard IW612 specification. | Board markings, `/proc/meminfo`, eMMC capacity, and IW612 SDIO probe on the received board |
| `FRDM-GATE-DISPLAY` | The DSI panel path (LCDIF, MIPI-DSI, `waveshare,dsi2dpi` bridge, 22-pin cable) is unobserved; the DRM card and connector that carry it are unknown. | DRM card/connector/mode listing at 1024×600 and a visible first frame on the panel |
| `FRDM-GATE-AUDIO` | The WM8962 capture path from the 3.5 mm CTIA headset microphone is unobserved. | ALSA capture device identity and a bounded capture with non-silent signal statistics |

## Alternatives considered

- **Buy another BeagleBone Black (~$60):** runs the current Buildroot image
  unchanged (`am335x-boneblack` DTS, `am335x_evm` U-Boot) and fits the 4D cape.
  Rejected: it spends money on a platform the assessment rejects for the
  product.
- **BeagleY-AI (~$70):** cheapest and most open. Rejected for now: no measured
  board suspend current or GPIO-wake proof, and it boots from microSD by
  default.
- **BeaglePlay ($99):** best public PIR-to-suspend example. Rejected for now:
  no NPU and 2 GB RAM, so local STT and gesture recognition would be marginal.
- **BeagleBone AI-64:** rejected in ADR 0003; add-on Wi-Fi only and a power
  supply of more than 3 A at 5 V.
- **RUBIK Pi 3 (QCS6490):** still gated on an explanation of its sleep-current
  claim.

## Consequences

- ADR 0003's decision to characterize the Black before buying a replacement is
  superseded. Its platform ranking and measurement contract remain in force.
- The [BeagleBone hardware baseline plan](../../.claude/plans/2026-08-05-beaglebone-hardware-baseline.md)
  cannot execute on this assembly. An FRDM hardware baseline plan should replace
  it, reusing its evidence discipline and observation contract.
- The Black-specific Buildroot defconfigs, the AM335x DeepSleep0 research, the
  4D cape backlight assumptions, and the P9_21 PIR pin remain in the repository
  as history. They are not targets.
- Board-independent software (supervisor policy, `panel_ui`, protocol v2,
  vocabulary, Lexinet client, structured logging) is unaffected. Device tree,
  display backend, presence wiring, suspend backend, and image recipe must be
  re-established.
- The [enclosure plan](../hardware/enclosure-plan.md) now targets the FRDM
  board and the 7-inch display. The board's 130 × 120 mm footprint and
  connector height make the first wall enclosure deeper than a product-grade
  SOM design would be.
- No hardware claim in this repository changes until the parts arrive and
  evidence is recorded under `hardware_tests/results/`.

## References

- [Compute platform assessment](../research/compute-platform-assessment.md)
- [ADR 0003](0003-characterize-beaglebone-before-selecting-product-platform.md)
- [FRDM-IMX8MPLUS board user manual UM12340](https://www.mouser.com/catalog/specsheets/NXP%20Semiconductors_08-07-2025_UM12340.pdf)
- [NXP Waveshare 7-inch (C) overlay](https://github.com/nxp-imx/linux-imx/blob/lf-6.12.y/arch/arm64/boot/dts/freescale/imx8mp-frdm-waveshare-7inch-c-panel.dtso)
- [Waveshare 7inch DSI LCD (C) wiki](https://www.waveshare.com/wiki/7inch_DSI_LCD_(C))
- [FRDM-IMX8MPLUS community hub](https://community.nxp.com/t5/FRDM-Training-Hub/FRDM-IMX8MPLUS-Hub/ta-p/2107907)

**Amended 2026-09-25:** added `FRDM-GATE-IDENTITY`, `FRDM-GATE-DISPLAY`, and
`FRDM-GATE-AUDIO` so the board-variant check, the DSI display path, and the
headset-microphone capture each have an explicit evidence gate. The existing
six gates are unchanged. No gate is closed; the gate list is also the vocabulary
for `UNMEASURED:<gate>` placeholders in `config/boards/frdm-imx8mp.env`.

**Amended 2026-09-25:** the replacement plan now exists
([FRDM hardware baseline plan](../../.claude/plans/2026-09-25-frdm-hardware-baseline.md)),
with one evidence script per gate in `hardware_tests/frdm/` and the ordered
board-arrival checklist in
[FRDM bring-up](../operations/frdm-bringup.md). The retarget also surfaced a
human decision this record did not name: the NXP Software License Agreement
covering `firmware-imx` (LPDDR4 training, required to boot a project image) and
the IW612 Wi-Fi firmware. It is tracked as
`HUMAN_DECISION_REQUIRED:NXP-FIRMWARE-EULA` in the board profile and must be
recorded as its own ADR if accepted. No gate is closed.
