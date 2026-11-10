<!-- @canonical architecture-decision-record-registry -->

# Architecture Decision Records

Architecture decision records use `NNNN-kebab-case.md` filenames and
`# ADR NNNN: Title` headings. A record's status is explicit; proposed records
still occupy their filename and are never treated as a free number.

## Current records

| ADR | Status | Decision |
|---|---|---|
| [0001](0001-separate-policy-from-presentation.md) | Accepted for the scaffold | Separate power policy from presentation |
| [0002](0002-buildroot-baseline.md) | Proposed; hardware-gated | Prefer Buildroot after Debian hardware bring-up |
| [0003](0003-characterize-beaglebone-before-selecting-product-platform.md) | Accepted; characterize-before-purchase decision superseded by ADR 0008 | Characterize BeagleBone before selecting the product platform |
| [0004](0004-freeze-review-gate-integration-contracts.md) | Accepted; protocol-v2 validation portions superseded by ADR 0007 | Freeze review-gate integration contracts |
| [0005](0005-validate-touch-provenance-at-capability-holders.md) | Accepted | Validate touch provenance at capability holders |
| [0006](0006-accept-product-semantics-and-atomic-protocol-v2-cutover.md) | Accepted | Accept product semantics and atomic protocol-v2 cutover |
| [0007](0007-validate-complete-protocol-v2-commitments.md) | Accepted | Validate complete protocol-v2 commitments |
| [0008](0008-adopt-frdm-imx8mplus-and-waveshare-7-inch-dsi-development-platform.md) | Accepted; hardware-gated | Adopt FRDM i.MX 8M Plus and Waveshare 7-inch DSI as the development platform |
| [0009](0009-add-an-isolated-virtual-panel-target.md) | Accepted for simulation | Share application code across headless, SDL2, and Debian/QEMU tiers |
| [0010](0010-use-node-capability-identity-and-pinned-official-tailscale-on-the-panel.md) | Accepted; Tailnet- and hardware-gated | Authenticate the panel by node capability and install pinned official Tailscale `1.102.2` |
| [0011](0011-blank-before-presentation-changes-not-every-cycle.md) | Accepted; checked on the FRDM panel 2026-10-18 | Blank before presentation changes, not every cycle |
| [0012](0012-keep-a-rotating-voice-lab-refresh-token-on-the-panel.md) | Accepted; waits for voice-lab's bundle and the uploader | Keep one rotating Pocket ID refresh token for voice-lab, held only by the uploader |
| [0013](0013-keep-a-spotify-refresh-token-on-the-panel.md) | Accepted; waits for the owner's Spotify app and sign-in | Keep one Spotify refresh token for playback control, held only by `bhp-music-control` |
| [0014](0014-run-a-bench-tier-on-the-vendor-image.md) | Accepted; not gate evidence | Run the project's services from `/opt/house-panel` on the vendor image, started at boot; install only the project's own files, a config directory and units |
| [0015](0015-use-open-meteo-and-ip-geolocation-for-the-daily-forecast.md) | Accepted | Daily forecast and place search from Open-Meteo; automatic location by IP geolocation through `free.freeipapi.com`; the weather location never moves day/night |

No later number is reserved by a plan.

**Amended 2026-09-25:** registered ADR 0009 for the user-requested isolated
virtual-panel target. Simulation never closes an FRDM hardware gate.

**Amended 2026-09-26:** registered ADR 0010 for the user's panel Tailnet
runtime choices: node-capability study authentication with no credential on the
panel, and the SHA-256-pinned official Tailscale `1.102.2` arm64 archive. Real
Tailnet evidence and every `FRDM-GATE-*` remain open.

**Amended 2026-10-18:** registered ADR 0011 at the owner's request, after the
FRDM demo showed the supervisor's every-cycle blank as visible flicker. A lit
panel with an unchanged display mode stays lit through the cycle. The
on-target check passed on the FRDM panel the same day (plan Step 4).

## Collision-safe allocation

1. When the decision record is actually created, scan every ADR filename on the
   current target branch and select the next unused four-digit prefix.
2. Add the file, heading, status, date, and any durable references using that
   prefix. Plans use `NNNN` placeholders until this moment.
3. Immediately before merging, re-scan the destination branch. If another
   record now occupies the prefix, renumber the new file, heading, and every
   reference before merge.
4. Run `make verify-docs` after allocation or renumbering.

This repository's numbering convention does not automatically apply to the
separate Lexinet repository. Lexinet records use whatever authority that
repository establishes.

**Amended 2026-08-07:** registered ADR 0004 as the authoritative record for
the six human decisions required by the 2026-08-06 review amendment plan.

**Amended 2026-08-07:** registered accepted ADR 0005 for the independently
reviewed touch-provenance and capability-holder trust boundary. This
registration does not approve roadmap Step 1 or any pending product decision.

**Amended 2026-08-16:** registered accepted ADR 0006 as the durable Roadmap
Step 1 product-semantics, shared-ownership, explicit-source-input, and atomic
protocol-v2 cutover decision. `AGENDA-011` and every implementation, feasibility,
and hardware gate remain pending.

**Amended 2026-08-16:** registered accepted ADR 0007 as the narrow superseding
record for ADR 0004's release-handshake and reduced protocol-v2 validation
sections. ADR 0004's other decisions remain accepted, and neither record claims
a physical display commit or target evidence.

**Amended 2026-09-24:** registered ADR 0008. The BeagleBone Black was lost;
the FRDM i.MX 8M Plus board and Waveshare 7inch DSI LCD (C) are being purchased
as the development platform. ADR 0003's characterize-before-purchase decision is
superseded; its platform ranking and measurement contract remain.

**Amended 2026-10-19:** registered ADR 0012, the owner's issue #23 decision to
keep a voice-lab refresh token on the panel, with its rules and threat model.
It is a narrow exception to ADR 0010, which still keeps every Lexinet
credential off the panel.

**Amended 2026-11-07:** registered ADR 0013, the owner's #69 decision to send
music to the Bose speaker through Spotify Connect, which keeps a Spotify
refresh token on the panel. It is a second narrow exception to ADR 0010, next
to ADR 0012.

**Amended 2026-11-10:** registered ADR 0014, the owner's decision to run a
bench tier on the vendor image (idea B1 of the November review). Its records
are not gate evidence, and it may install only the project's own files in
`/opt/house-panel`, a configuration directory and systemd units.

**Amended 2026-11-10:** registered ADR 0015, the owner's acceptance of the
weather plan's five decisions: Open-Meteo, automatic location by IP
geolocation, day and night left on the supervisor's location, a drawn
keyboard, and the defaults.
