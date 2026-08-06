# Custom PIR input-driver experiment

Status: source-level prototype; target compilation, overlay application, IRQ
delivery, suspend entry, and wake have not been proven on hardware.

This experiment binds `beagle,house-pir` to the local
`beagle_house_pir_input_driver` module. It reports the logical GPIO state as
`EV_SW/SW_FRONT_PROXIMITY`, requests rising and falling edges, and attaches the
same GPIO IRQ to the PM wake-IRQ framework when `wakeup-source` is present.

Artifacts:

- `beagle_house_pir_input_driver.c`: custom platform/input driver.
- `beagle-house-custom-pir-overlay.dts`: candidate BeagleBone Black P9.21 /
  GPIO0_3 wiring.
- `Documentation/devicetree/bindings/input/beagle,house-pir.yaml`:
  experimental firmware contract.

Use the stock `gpio-keys` experiment first. A baseline failure points away from
the custom module and toward the electrical, pinmux, IRQ-controller, firmware,
or AM335x low-power path.
