# Process Interconnectivity

**Status:** the first three diagrams document the implemented but disabled
scaffold. They do not describe the proposed production owner or claim that a
Lexinet study API exists. The planned on-panel and Lexinet successor slice is
shown separately; agenda-to-Mac provider and message flows remain in the
personal-agenda architecture.

## Current scaffold runtime topology

```mermaid
flowchart LR
    EVDEV["/dev/input/event*\nPIR state"] -->|"read-only"| SUP["panel_supervisor\nprivileged service user"]
    SUP -->|"write validated integer"| BL["/sys/class/backlight/.../brightness"]
    SUP -->|"write mem/freeze"| STATE["/sys/power/state"]
    SUP -->|"JSON Lines, local Unix socket"| UI["vocabulary_panel\nunprivileged user"]
    UI -->|"HTTPS request"| SOCK["tailscaled / kernel TUN"]
    SOCK --> LEX["Lexinet"]
    UI -->|"DRM/framebuffer + evdev"| DISPLAY["display/touch"]
```

Only the supervisor receives permissions for backlight and suspend. In this
legacy scaffold, the vocabulary process can connect to the supervisor socket
and display/input devices, but it cannot change system power directly. The
planned neutral `panel_ui` replaces that device owner before production.

## Current scaffold daytime sequence

```mermaid
sequenceDiagram
    participant P as Presence provider
    participant S as Panel supervisor
    participant U as Vocabulary panel
    participant T as tailscaled
    participant L as Lexinet

    P->>S: asserted state event
    S->>S: read clock; calculate solar period
    S->>U: mode.v1 {display_mode: vocabulary}
    S->>S: apply calibrated day brightness
    U->>T: HTTPS next-card request
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
    participant U as Vocabulary panel
    participant K as Kernel PM
    participant T as tailscaled

    P->>S: deasserted state event
    S->>S: enter grace period
    S->>S: grace expires; re-read current state
    S->>S: apply brightness 0
    S->>U: mode.v1 {display_mode: suspended}
    S->>K: request selected suspend experiment
    Note over K,T: network and tailnet become unavailable in DeepSleep0
    P->>K: GPIO0 wake interrupt
    K-->>S: process resumes
    S->>S: re-read GPIO + clock + solar truth
    S->>U: recomputed mode; never blind-toggle
    S->>S: v1 applies brightness after socket delivery
    Note over S,U: render-ready/page-flip acknowledgement is not implemented yet
    T-->>T: re-establish tailnet asynchronously
```

## Local mode protocol

The supervisor is authoritative. The UI cannot command a mode; version 1 only
delivers snapshots/events. With no connected UI, publication is deliberately a
successful no-op so power policy cannot be held hostage by a crashed display
process. A future sequence-bound render-ready acknowledgement may delay
nonzero brightness without granting the UI power authority. See
[panel mode protocol](../interfaces/panel-mode-protocol.md).

## Proposed production panel/Lexinet convergence slice

```mermaid
flowchart LR
    PIR["PIR / attributed input"] --> SUP["panel_supervisor\npower + lifecycle authority"]
    SUP -->|"mode.v2 snapshot / commit deadline"| SHELL["panel_ui\nsole display + touch owner"]
    SHELL --> STUDY["vocabulary reducer\ncache + outbox"]
    SHELL --> AGENDA["agenda reducer"]
    CATALOG["spoken-command catalog"] --> SET["ActiveCommandSet"]
    SET --> PROJ["content-addressed backend projection"]
    POLICY["validated recognition-policy artifact"] --> BROKER
    MIC["microphone"] --> BROKER["media broker\nraw capture owner"]
    PROJ --> BROKER
    BROKER -->|"artifact-bound semantic intent"| SHELL
    SHELL --> DISPLAY["DRM/fbdev + touch"]
    STUDY -->|"HTTPS study.v1"| TAIL["tailscaled / kernel TUN"]
    TAIL --> LEX["Lexinet study service"]
```

This local-runtime and Lexinet slice is proposed and remains disabled pending
the Step 1 decisions, protocol-v2 work, server-owned `study.v1` contract, shared
shell, catalog pipeline, and cross-repository tests. It intentionally stops at
the agenda reducer; the signed personal-context client, recording-artifact
transfer, Tailnet route, and Mac companion are documented in
[Personal Agenda and Birthday Wishes](personal-agenda-and-birthday-wishes.md).

## Lexinet boundary

Tailscale supplies routing and node identity. Lexinet must still apply narrow
study authorization. API data is untrusted until the server-owned schema and
panel validation both succeed. See the [Lexinet consumer transition
contract](../interfaces/lexinet-contract.md) and [threat
model](../security/threat-model.md).
