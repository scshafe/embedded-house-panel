# Lexinet Flash-Card UX

**Status:** implemented scaffold behavior. The pending `LEXI-001` through
`LEXI-007` review in the
[Lexinet study architecture](../architecture/lexinet-study-integration.md)
proposes a two-sided card with explicit flip/next/back semantics. If approved,
that decision supersedes this progression before production controller work;
the project must not implement both interaction models.

## Current scaffold UX goals

- Work at a likely 480×272 resolution with large touch targets.
- Document the implemented legacy progression: word → definition → example
  → rating → next. It is evidence for the rewrite, not the proposed version 1
  product contract.
- Never flash a white surface while entering or leaving after-hours mode.
- Make loading, offline, empty, and failed states visibly different.
- Keep Lexinet authoritative; never imply that an unsubmitted review was saved.

## Current scaffold interaction

```mermaid
stateDiagram-v2
    [*] --> Loading
    Loading --> Word: valid card
    Loading --> Offline: network unavailable + explicit bounded cache
    Loading --> Empty: successful response has no card
    Loading --> Error: invalid response / permanent failure
    Word --> Definition: tap card
    Definition --> Example: tap card
    Example --> Rating: tap card or choose rate
    Rating --> Submitting: choose Again / Hard / Good / Easy
    Submitting --> Loading: review acknowledged
    Submitting --> Error: review rejected; retain current card
    Offline --> Word: cached card selected with degraded badge
    Example --> OfflineComplete: cached/degraded card; never offer rating
    OfflineComplete --> Loading: retry connection
    Error --> Loading: retry
    Empty --> Loading: refresh
```

## Current scaffold supervisor-driven flow

```mermaid
flowchart LR
    VOCAB["Vocabulary mode"] --> WORD["Word"] --> DEF["Definition"] --> EX["Example"] --> RATE["Rate"] --> VOCAB
    VOCAB -->|"night event"| NIGHT["Dim red night surface"]
    NIGHT -->|"day event"| VOCAB
    VOCAB -->|"suspend event"| BLANK["Black frame; release rendering"]
    NIGHT -->|"suspend event"| BLANK
    BLANK -->|"resume + recomputed day"| VOCAB
    BLANK -->|"resume + recomputed night"| NIGHT
```

## Current scaffold wireframes

```text
┌────────────────────────────────────────┐
│  07:42                    ● tailnet     │
│                                        │
│              perspicacious             │
│                                        │
│         Tap anywhere to reveal         │
│                                        │
│                             next ▶      │
└────────────────────────────────────────┘

┌────────────────────────────────────────┐
│  Definition                            │
│                                        │
│  Having a ready insight into things;   │
│  shrewd and perceptive.                 │
│                                        │
│           Tap for an example           │
└────────────────────────────────────────┘

┌────────────────────────────────────────┐
│  How well did you know it?             │
│                                        │
│  [ Again ] [ Hard ] [ Good ] [ Easy ]  │
│                                        │
│  Review is saved only after Lexinet ✓  │
└────────────────────────────────────────┘
```

This scaffold's four rating buttons may be too narrow for reliable touch at 480
pixels. The proposed `LEXI-005` contract makes ratings available through
equivalent touch and voice actions after the back is revealed. On-target
measurement still decides the touch geometry and whether a real voice engine
qualifies for production.

## Error and offline screens

| State | Copy | Allowed action |
|---|---|---|
| Loading | “Connecting to Lexinet…” | Cancel only after a bounded timeout |
| Offline, cache present | “Offline — cached card; reviews are not being saved” | Study or retry; no silent review queue |
| Offline study complete | “Reconnect to save reviews” | Retry; rating controls remain unavailable |
| Offline, no cache | “Lexinet unavailable” | Retry |
| Empty | “No review card available” | Refresh |
| Response invalid | Machine-readable error code plus short explanation | Retry after contract/config correction |

## Night surface

Night mode is not a dimmed daytime UI. It is a separate solid-red rendering path with no network calls or white intermediate frames. The exact RGB value and brightness are calibrated on the cape in the dark.
