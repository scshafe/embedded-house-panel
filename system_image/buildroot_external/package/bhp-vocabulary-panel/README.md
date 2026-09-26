# Buildroot package: vocabulary panel

This directory is the canonical Buildroot glue for the vocabulary work stream.
It builds the state machine, fixture terminal, LVGL renderer, and provisional
HTTPS Lexinet adapter. The adapter links Buildroot's libcurl, OpenSSL provider,
CA certificate bundle, and json-for-modern-cpp; compilation is useful boundary
evidence but does not validate the assumed Lexinet routes or payloads.

Public Buildroot interface:

- Kconfig symbol: `BR2_PACKAGE_BHP_VOCABULARY_PANEL`
- package target: `bhp-vocabulary-panel`
- dependencies: `lvgl`, `libcurl`, and `json-for-modern-cpp`
- target user: none (retired 2026-09-26)
- installed fixture executable: `/usr/bin/vocabulary_panel_terminal`

The vocabulary feature runs beneath the neutral `bhp-panel-ui` shell, so this
package installs fixtures and libraries only, with no service or principal.
Local-source builds reflect the exact outer
repository checkout. Release verification uses a fresh output named by the
exact project content ID, preventing an earlier local-package stamp from being
accepted.

**Amended 2026-09-25:** ADR 0005 removes the legacy vocabulary user's raw-touch
group. All evdev nodes remain root-only, including an exactly provisioned touch
device; only the supervisor may observe them. The neutral UI consumes the
separate normalized observation socket. Production services remain disabled
and fail-dark pending FRDM display/touch evidence.

**Amended 2026-09-26:** retired the standalone `vocabulary-panel` principal,
its `S80bhp-vocabulary-panel` service and its environment example. The
vocabulary feature is hosted by `panel_ui`, whose reserved `panel-ui.env`
section carries the `study.v1` settings. This also removes the image's only
display grant. `panel-ui` joins `video` at the `FRDM-GATE-DISPLAY` cutover
(`system_image/service-principals.json`).
