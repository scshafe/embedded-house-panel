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
- target user: `vocabulary-panel` (non-login, `video` and `bhp-touch` groups)
- installed fixture executable: `/usr/bin/vocabulary_panel_terminal`

No graphical controller executable exists yet, so
`S80bhp-vocabulary-panel` is disabled and expects the future
`/usr/bin/bhp-vocabulary-panel`. Local-source builds reflect the exact outer
repository checkout. Release verification uses a fresh output named by the
exact project content ID, preventing an earlier local-package stamp from being
accepted. The `bhp-touch` group receives only the exact provisioned evdev
name+phys pair; it never receives blanket access to `/dev/input/event*`.
