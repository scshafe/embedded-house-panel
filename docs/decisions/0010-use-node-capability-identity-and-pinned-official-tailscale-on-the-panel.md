<!-- @canonical panel-tailnet-runtime-decision -->

# ADR 0010: Use Node-Capability Identity and Pinned Official Tailscale on the Panel

**Status:** accepted by the user on 2026-09-26; deployment remains gated on real
Tailnet evidence and FRDM hardware evidence

**Date:** 2026-09-26

## Context

Lexinet decision `0003-scope-study-tailnet-access` offers two mutually exclusive
startup modes for its native-TLS Tailnet study listener. Node-capability mode
authorizes each request from fresh `WhoIs` transport identity, exactly one
configured source tag and an application-capability grant. Scoped-token mode
also requires `WhoIs`, but authorizes from an owner-only token file.

Lexinet pins Tailscale `v1.102.2`, which requires Go `1.26.5`. The verified
Buildroot `2026.05.1` input supplies Tailscale `1.78.1` and Go `1.26.3`. The
Lexinet integration plan (Step 5) forbids silently substituting an older
toolchain-compatible release: the panel must choose either a SHA-256-pinned
official ARM binary package or an approved source-build toolchain upgrade.

## Decision

1. **Panel authentication is node-capability mode.** The panel is enrolled as a
   Tailnet node with exactly the one configured source tag. Lexinet's
   `tailnet_node` mode authorizes create/resume and sync from that identity and
   the application-capability grant. The panel stores no study bearer or scoped
   token; its production `study.v1` configuration leaves the credential file
   empty. Lexinet's loopback operator bearer never reaches the panel, its image
   or the virtual panel.
2. **Tailscale comes from the official static archive.** The panel image
   installs Tailscale `1.102.2` from Tailscale's official static `arm64`
   archive, verified by a SHA-256 recorded in the Buildroot external tree. It
   replaces Buildroot's source-built `tailscale` package for this image; Buildroot's
   host Go stays unchanged. The panel and Lexinet versions move together: a
   version change updates Lexinet's pin, the external-tree hash and this record
   in one reviewed change.
3. **Node state is identity, not image content.** The `tailscaled` state
   directory lives on persistent storage outside the image, owned by the
   Tailscale service principal with mode `0700`. Enrollment keys, node state,
   certificates and tokens never enter Git, images, overlays, logs, command
   arguments, URLs or diagnostics.
4. **Lexinet's long-term host is deferred.** Tests use a disposable Lexinet.
   Deployment instructions keep the host a named placeholder until the user
   chooses it.

## Evidence and gates

This record changes no evidence gate. Production enablement still needs the
sanitized real-Tailnet matrix from Lexinet Step 5 (tagged, untagged, wrong-tag,
live grant/tag revocation, certificate/hostname/TLS), labelled
`real_tailnet_not_panel_hardware`, and every applicable `FRDM-GATE-*` from
ADR 0008. The archive `tailscale_1.102.2_arm64.tgz` has SHA-256
`2b64e9ade7e73034b5ec9e9bcd537f5ddd14ae3abb435e57e929e7486ae42660`, recorded
from the official `.sha256` file on 2026-09-26. The first Mac-local image build
must re-download that file and fail closed on any mismatch. Buildroot's package
hash check also fails closed.

The `bhp-tailscale-official` external-tree package implements item 2 and
keeps Buildroot's install layout (`/usr/bin/tailscale`, `/usr/bin/tailscaled`
and the `/usr/sbin/tailscaled` link) and kernel configuration fixups.
`make verify-docs` fails if the package version, its hash line and this record
disagree.

## Alternatives

- **Scoped-token mode** keeps a secret on a physically accessible device and
  needs rotation after any suspected exposure. Rejected.
- **Pinned source build** keeps a source-built binary, but needs a Buildroot host
  Go upgrade to `1.26.5`, a vendored-module hash and ongoing toolchain upkeep.
  Rejected by the user in favour of exact version parity with Lexinet.

## References

- [Pre-board completion plan](../../.claude/plans/2026-09-25-pre-board-completion.md)
- [Lexinet study integration plan, Step 5](../../.claude/plans/2026-08-05-lexinet-study-integration.md)
- [Lexinet consumer contract](../interfaces/lexinet-contract.md)
- [FRDM platform](0008-adopt-frdm-imx8mplus-and-waveshare-7-inch-dsi-development-platform.md)
- [Virtual panel target](0009-add-an-isolated-virtual-panel-target.md)
