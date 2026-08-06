# Hardware Assumptions Register

Every assumption remains provisional until linked evidence exists under `hardware_tests/results/`.

| Assumption | Legacy clue | Risk | Evidence required |
|---|---|---|---|
| Board is BeagleBone Black with 4 GB eMMC | Saved device tree and developer recollection | Different board/revision changes boot, pins, and PM | Markings, EEPROM, block layout |
| Display is a 4D 480×272 cape | Legacy GTK window and backlight path | Model/revision/touch stack unknown | Cape markings/EEPROM, live DT, DRM/fb/input discovery |
| Backlight exposes 0–100 | Legacy code and 4D documentation | Node name/range and “off” behavior vary | `max_brightness`, measured current, visual cycle test |
| PIR has one 3.3 V-safe state output | Typical PIR behavior | Legacy code used two pins; voltage may be unsafe | Exact part datasheet and isolated powered measurement |
| P9_21/GPIO0_3 is usable | Legacy overlay | Cape may consume pin; mux/wake configuration may differ | Pin conflict audit and live GPIO ownership |
| GPIO0 can wake chosen DeepSleep0 | AM335x documentation | Kernel/PM firmware/cape may prevent it | Repeated wake test with wake-source evidence |
| Tailnet returns promptly after resume | Product intent | Interface/DHCP/Tailscale latency may be user-visible | Timed network and peer-restoration measurements |

The official 4D brightness interface and AM335x wake documentation are summarized in the research documents; neither replaces measurement on this assembly.

