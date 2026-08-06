# Stock gpio-keys wake baseline experiment

Status: overlay prototype; target compilation, overlay application, IRQ
delivery, suspend entry, and wake have not been proven on hardware.

This experiment deliberately contains no custom driver code. Its overlay uses
Linux v6.12's in-tree `gpio-keys` driver on the same candidate P9.21 /
GPIO0_3 input and emits the same `EV_SW/SW_FRONT_PROXIMITY` state as the
custom experiment. Both asserted and deasserted edges are eligible wake events.

Required target options include `CONFIG_KEYBOARD_GPIO` and, for userspace event
devices, `CONFIG_INPUT_EVDEV`. Build and test this baseline before attributing
any wake failure to the custom module.
