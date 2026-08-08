# Process Interconnectivity

**Status:** the first three diagrams document the implemented but disabled
scaffold, including the version 1 publisher ending at an unconsumed socket.
They do not describe the proposed production owner or claim that a Lexinet
study API exists. The planned on-panel and Lexinet successor slice is shown
separately; agenda-to-Mac provider and message flows remain in the
personal-agenda architecture.

## Current scaffold runtime topology

```mermaid
flowchart LR
    EVDEV["/dev/input/event*\nPIR state"] -->|"read-only"| SUP["panel_supervisor\nprivileged service user"]
    SUP -->|"write validated integer"| BL["/sys/class/backlight/.../brightness"]
    SUP -->|"write mem/freeze"| STATE["/sys/power/state"]
    SUP -->|"publish JSON Lines"| MODE["panel-mode.sock\nversion 1, unconsumed"]
    UI["vocabulary_panel\nunprivileged user"]
    UI -->|"HTTPS request"| SOCK["tailscaled / kernel TUN"]
    SOCK --> LEX["Lexinet"]
    UI -->|"DRM/framebuffer + evdev"| DISPLAY["display/touch"]
```

Only the supervisor receives permissions for backlight and suspend. In this
legacy scaffold, ~~the vocabulary process can connect to the supervisor socket
and display/input devices~~ the supervisor publishes version 1 to its local
socket, but `vocabulary_panel` implements no `AF_UNIX` client and does not
consume those snapshots. The vocabulary process remains unprivileged and
cannot change system power directly. The planned neutral `panel_ui` replaces
that device owner before production.

## Current scaffold daytime sequence

```mermaid
sequenceDiagram
    participant P as Presence provider
    participant S as Panel supervisor
    participant M as Panel-mode socket (unconsumed)
    participant U as Vocabulary panel
    participant T as tailscaled
    participant L as Lexinet

    P->>S: asserted state event
    S->>S: read clock; calculate solar period
    S->>M: publish mode.v1 {display_mode: vocabulary}
    Note over M,U: no implemented client carries this snapshot to U
    S->>S: apply calibrated day brightness
    U->>T: provisional HTTPS next-card request (not mode-triggered)
    T->>L: route over tailnet
    L-->>U: validated response envelope
    U-->>U: word screen
```

The `next-card` exchange above is fixture/provisional behavior, not an existing
Lexinet endpoint or the planned `study.v1` session contract.

## Absence and resume

```mermaid
sequenceDiagram
    participant P as Presence provider
    participant S as Panel supervisor
    participant M as Panel-mode socket (unconsumed)
    participant K as Kernel PM
    participant T as tailscaled

    P->>S: deasserted state event
    S->>S: enter grace period
    S->>S: grace expires; re-read current state
    S->>S: apply brightness 0
    S->>M: publish mode.v1 {display_mode: suspended}
    S->>K: request selected suspend experiment
    Note over K,T: network and tailnet become unavailable in DeepSleep0
    P->>K: GPIO0 wake interrupt
    K-->>S: process resumes
    S->>S: re-read GPIO + clock + solar truth
    S->>M: publish recomputed mode; never blind-toggle
    S->>S: v1 applies brightness after retaining the snapshot
    Note over S,M: publication is unconsumed; no render-ready acknowledgement exists
    T-->>T: re-establish tailnet asynchronously
```

## Local mode protocol

The supervisor is authoritative. The UI cannot command a mode. ~~Version 1 only
delivers snapshots/events.~~ Version 1 publishes snapshots/events, but no
implemented process consumes them. With no connected consumer, publication is
deliberately a successful no-op so power policy cannot be held hostage by a
crashed display process. A future sequence-bound render-ready acknowledgement
may delay nonzero brightness without granting the UI power authority. See
[panel mode protocol](../interfaces/panel-mode-protocol.md).

## Proposed production panel/Lexinet convergence slice

```mermaid
flowchart LR
    PIR["PIR / attributed input"] --> SUP["panel_supervisor\npower + lifecycle authority"]
    TOUCH["exact configured touch evdev"] -->|"direct observation"] SUP
    SUP -->|"mode.v2 snapshot / commit deadline"| SHELL["panel_ui\nsole display + application touch router"]
    SUP -->|"separate one-way normalized touch observation + opaque ID"| SHELL
    SHELL --> STUDY["vocabulary reducer\ncache + outbox"]
    SHELL --> AGENDA["agenda reducer"]
    CATALOG["spoken-command catalog"] --> SET["ActiveCommandSet"]
    SET --> PROJ["content-addressed backend projection"]
    POLICY["validated recognition-policy artifact"] --> BROKER
    MIC["microphone"] --> BROKER["media broker\nraw capture owner"]
    SUP -->|"lifecycle window; close on absence / night / suspend preparation"| BROKER
    SHELL -->|"direct purpose-scoped lease request + touch observation ID"| BROKER
    BROKER -->|"exact-peer claim for canonical operation digest"| SUP
    PROJ --> BROKER
    BROKER -->|"artifact-bound semantic intent"| SHELL
    SHELL --> DISPLAY["DRM/fbdev + application touch routing"]
    STUDY -->|"HTTPS study.v1"| TAIL["tailscaled / kernel TUN"]
    TAIL --> LEX["Lexinet study service"]
```

This local-runtime and Lexinet slice is proposed and remains disabled pending
the Step 1 decisions, protocol-v2 work, server-owned `study.v1` contract, shared
shell, catalog pipeline, and cross-repository tests. It intentionally stops at
the agenda reducer; the signed personal-context client, recording-artifact
transfer, Tailnet route, and Mac companion are documented in
[Personal Agenda and Birthday Wishes](personal-agenda-and-birthday-wishes.md).

~~The diagram's former `panel_ui`-owned touch edge left the UI able to author
the same touch claim consumed by a capability holder.~~ Under proposed
[ADR 0005](../decisions/0005-validate-touch-provenance-at-capability-holders.md),
the supervisor observes the exact configured touch device and forwards
normalized events on a separate one-way stream. The UI maps those events to
application actions, then requests media leases directly from the broker. Only
the broker or personal-context client can claim an observation from the
supervisor and bind it once to an opaque canonical operation digest. Those
claimants use distinct exact service UIDs verified with `SO_PEERCRED`; group
membership cannot substitute, and each claimant reciprocally verifies the
supervisor UID and root-owned socket identity. Version, supervisor session, and
sequence bind lifecycle, observation, and provenance messages. Disconnect,
restart, stale session, replay, or an unreconciled gap closes the media window
and broker leases, and invalidates unclaimed observations or in-flight claim
exchanges. It does not revoke a confirmation token already created by the
personal-context client, which has no lifecycle subscription and applies its
own expiry/restart/consumption rules. Cutover removes the UI's current raw
`bhp-touch` access, and slow observation/provenance consumers are dropped so
they cannot delay blanking or suspend. This does not add any request, lease,
feature message, or capability negotiation to the UI-to-supervisor lifecycle
socket.

## Lexinet boundary

Tailscale supplies routing and node identity. Lexinet must still apply narrow
study authorization. API data is untrusted until the server-owned schema and
panel validation both succeed. See the [Lexinet consumer transition
contract](../interfaces/lexinet-contract.md) and [threat
model](../security/threat-model.md).

**Amended 2026-08-07:** corrected the current topology and sequences to show
that the supervisor publishes protocol version 1 while the socket remains
unconsumed; the future neutral `panel_ui` remains the first planned consumer.

**Amended 2026-08-07:** added the proposed ADR 0005 touch-provenance path,
direct UI-to-broker lease request, supervisor lifecycle publication, and
capability-holder claim boundary. These are future contracts, not implemented
code, hardware evidence, roadmap approval, or product-decision approval.
