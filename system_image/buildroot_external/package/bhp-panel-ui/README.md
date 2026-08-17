# Buildroot package: panel UI

This directory is the canonical Buildroot glue for the neutral `panel_ui`
shell. It builds from the repository root so the executable consumes the same
canonical panel-mode protocol, OpenSSL implementation, and pre-derived
application release identity as the supervisor.

Public Buildroot interface:

- Kconfig symbol: `BR2_PACKAGE_BHP_PANEL_UI`
- package target: `bhp-panel-ui`
- direct dependency: `openssl`
- installed lifecycle executable: `/usr/bin/bhp-panel-ui`
- target principal: `panel-ui` with fixed UID/GID `1102`

UID 1102 is part of the local protocol deployment contract, not an automatic
Buildroot allocation. `panel_supervisord` compares the peer UID obtained from
`SO_PEERCRED` with this exact value. Renumbering therefore requires one atomic
image/configuration cutover for both peers.

The `panel-ui` principal has a locked password, `/bin/false` login shell, no
home directory, and no supplementary groups. In particular, it has no
`bhp-touch`, generic `input`, or other raw-input membership. The installed
executable needs and receives no display-device membership because no physical
display commit backend exists yet.

The package refuses configuration unless the canonical Buildroot wrapper has
already derived `BHP_APPLIANCE_RELEASE_ID`. It passes that value unchanged to
the top-level CMake build. This avoids independently deriving plausible but
different identities from Buildroot's local-source copy. OpenSSL is listed as
a direct package dependency because the shared protocol uses its EVP digest
implementation; the dependency is not left implicit through another package.

`/usr/bin/bhp-panel-ui` has no runtime defaults. It requires explicit lifecycle,
reconnect, controller, Unicode, and active-surface values. The persistent
service authenticates the supervisor, validates every ordered lifecycle
snapshot, renders the corresponding neutral headless frame, and reports the
current fail-dark backend's exact-generation `failed` presentation result. A
failed result is never readiness and cannot permit nonzero brightness. The
separate host-test smoke executable is not installed and never consumes
lifecycle state.

`S75bhp-panel-ui` remains disabled and refuses an attempted enablement until an
evidence-backed physical display commit backend can produce `committed` for the
exact rendered generation. The installed process already has bounded reconnect
and stop semantics; its backend intentionally keeps the appliance dark. A
successful package build is host-neutral software evidence only and makes no
target display, touch, power, wake, or readiness claim.
