<!-- @canonical macos-personal-companion-feasibility-contract -->

# macOS Personal Companion Feasibility Contract

**Status:** proposed for human safety review; no probe or external effect is
authorized by this document

**Scope:** isolated fixture-first evidence for the personal-agenda feasibility
plan. This directory is not a production companion, provider, transport, or
service boundary.

## Authority and non-goals

[ADR 0006](../../docs/decisions/0006-accept-product-semantics-and-atomic-protocol-v2-cutover.md)
accepts `AGENDA-001` through `AGENDA-010` and deliberately defers
`AGENDA-011`. The
[personal-agenda architecture](../../docs/architecture/personal-agenda-and-birthday-wishes.md)
remains the product authority. This contract freezes only how feasibility will
be measured.

The experiment may use synthetic fixtures without personal-data permission. It
does not authorize provider reads, Calendar or Contacts mutation, microphone
capture, a live Messages adapter, recipient provisioning, Tailscale Serve
configuration, login-item enablement, or a production default. `DraftOnlyTransport`
is the only enabled transport until the later live-transport stop point is
separately approved.

Audio-file submission remains disabled unless complete local playback succeeds;
a duration, waveform, partial playback, or successful encoding is not a review.
Voice recognition may only open the reversible `agenda.prepare_wish` composer.
It cannot create or activate a `wish_recording` lease, begin capture, confirm
content, or submit. Any future recording starts only from an affirmative,
provenance-checked touch after the composer is visible. While that purpose is
active, the media broker may expose only its purpose-internal stop-only command
set containing `agenda.stop_wish_recording`; the general command grammar cannot
stop capture, and the stop intent can never submit.

Production shell, media-broker, HTTP-runtime, intent-catalog, personal-context,
and Buildroot capabilities remain owned by their canonical domains. Nothing in
this experiment may create a peer implementation.

## Observed host prerequisites

The environment inventory is content-free and may be refreshed before a probe:

| Prerequisite | Observation on 2026-08-16 | Gate |
|---|---|---|
| macOS | 26.6.1 build 25G76 | Record exact OS build with every result |
| Developer tools | `/Library/Developer/CommandLineTools`; `xcodebuild` unavailable | Step 2 is blocked until full Xcode is selected |
| Code signing | One valid code-signing identity exists; its label and hash are not recorded | Human must select one stable application identity before Step 2 |
| Tailscale | 1.98.10 | Evidence only; no Serve or grant change is authorized here |

No probe may substitute an unsigned command-line tool, root daemon, ad-hoc
identity, or mock permission result for the visible signed application required
by the plan.

## Stable outcome model

Every probe returns exactly one of these content-free outcomes:

| Outcome | Meaning |
|---|---|
| `available` | The bounded capability is present or the fixture operation completed; no Messages handoff occurred |
| `permission_denied` | The operating system, user, or signed-app permission boundary denied the operation |
| `unsupported` | The exact OS/framework/transport combination cannot provide the requested bounded capability |
| `failed_before_submission` | A transport attempt ended before its durable `dispatch_started` boundary and no external handoff was invoked |
| `submitted` | The selected adapter returned success after exactly one handoff invocation; this says nothing about delivery, presentation, or reading |
| `delivery_unknown` | A timeout, crash, restart, or ambiguous error occurred at or after `dispatch_started`; automatic retry is permanently forbidden |

`sent`, `delivered`, `received`, and `read` are not experiment outcomes. Recovery
of an unfinished `dispatch_started` record produces `delivery_unknown` before
content deletion and never replays the adapter.

## Approved data classes

- Fixture identities, calendars, contacts, meetings, birthdays, routes, text,
  audio, and transcripts are deterministic and obviously synthetic.
- A live provider probe may expose personal data only inside the signed app
  after visible opt-in and source selection. Results contain only the allowlisted
  metadata below.
- A live Messages route must be either the operator's own self route or a
  dedicated test route wholly controlled by the same operator. A friend,
  household member, group conversation, work contact, or arbitrary recipient is
  never a feasibility target.
- The exact route, address, sender identity, account label, calendar/contact
  source label, Shortcut name, and TCC record remain local and are never copied
  into this repository or a diagnostic artifact.
- Live marker text is restricted to a visibly synthetic, non-personal
  feasibility marker plus a local run ordinal. It contains no name, birthday,
  greeting, contact fact, or conversational content.
- A live audio attachment is a deterministic generated non-speech tone. It is
  never a recording of a person or room. Synthetic speech used to test local
  transcription is a separate fixture and is never submitted through Messages.

## Hard experiment ceilings

These are ceilings, not production defaults. Evidence may reduce them; raising
one requires another human review.

| Boundary | Ceiling |
|---|---|
| Live Messages side effects | Zero by default. One later explicit approval may authorize at most 12 total handoff attempts, no more than one per matrix cell and no more than two audio attachments |
| Recipient scope | Exactly one locally provisioned self/test route for an approved run; no runtime search, display-name resolution, group, or alternate route |
| Synthetic live audio | At most 3 seconds and 256 KiB after encoding |
| Capture/transcription fixture | At most 60 seconds and 4 MiB; never a live Messages attachment unless it independently meets the smaller live-audio ceiling |
| Mac draft content | Mode `0600`, parent directory mode `0700`, hard expiry 30 minutes from creation, with all earlier terminal-deletion rules |
| Incomplete upload/staging | Delete on failure; startup sweep deletes every partial artifact before accepting work |
| Non-content attempt metadata | Hard expiry 30 days; no body/audio/transcript, content hash, route handle, sender, or provider identifier |
| Provider read | Explicitly selected sources and the accepted birthday window only; no write API is reachable |
| Operation timeout before handoff | 15 seconds; timeout returns a typed non-submission outcome |
| Messages automation after `dispatch_started` | 30 seconds; timeout becomes `delivery_unknown` and consumes the single-use attempt |
| Permission prompts | At most one deliberate request per framework per signed build/run; never reset TCC automatically |
| Login-item changes | At most one explicit enable and one explicit disable per reviewed run |

One live approval expires when its handoff-attempt ceiling is reached, the app
or transport revision changes, the provisioned route/sender revision changes,
the OS or Messages build changes, or any unexpected recipient/content behavior
occurs. A new approval is then required.

## Redaction and evidence policy

Tracked results and logs use an allowlist. They may contain:

- OS, application, framework, tool, and adapter versions;
- synthetic fixture IDs that are visibly marked as fixtures;
- the stable outcome enum and structured error code;
- boolean capability/permission state;
- bounded aggregate counts without labels; and
- rounded durations and byte counts that cannot reconstruct content.

They must not contain names, handles, addresses, account/source labels,
calendar titles, event bodies, meeting locations, birthdays, message bodies,
audio, transcripts, recognition output, content hashes, local file paths,
Shortcut names, Tailnet state, TCC database content, signing identity details,
credentials, tokens, or request/response payloads. Hashing personal content does
not make it safe diagnostic data.

Runtime logging is structured and content-free. Errors expose only stable code,
message, and an allowlisted context such as operation, bound, adapter kind, or
outcome. A redaction test supplies canary personal-shaped values to every error
and diagnostic path and proves none appear. Process-list and network-capture
review must also find no recipient, message, audio, transcript, credential,
file path, or generated automation source.

Any unavoidable local evidence is reviewed in place and reduced to the
allowlist before a result is written. Raw evidence and staging are ignored,
mode-restricted, and deleted at the end of the run or earlier on failure.

## Manual stop points

Execution stops at every boundary below; a later approval never rolls forward
to another boundary.

1. **Before signed-app work:** select full Xcode, the stable bundle/signing
   identity, and the fixture-only build recipe. Until then no native target is
   created or signed.
2. **Before each OS permission request:** the visible app shows the exact
   framework, logical data subset, and disable path. The local operator must opt
   in; denial is a valid terminal observation.
3. **Before the first personal provider read:** the local operator selects the
   permitted calendars/contact scope after content-free discovery. No result is
   written until redaction review passes.
4. **Before Tailnet or background changes:** separately review the exact Serve
   configuration, app-capability grants, login-item change, listener address,
   and rollback. Step 1 authorizes none of them.
5. **Before enabling a live Messages adapter:** obtain conversational approval
   naming the local self/test recipient class, exact maximum handoff count,
   marker-text class, synthetic audio fixture, sender/route revision, transport,
   and matrix cells. Default remains zero.
6. **Immediately before every live handoff:** the visible signed app displays
   the masked route, sender identity, transport, attempt ordinal, and exact text
   or complete audio-playback result. The operator confirms once; confirmation
   is single-use.
7. **After any ambiguous handoff:** record `delivery_unknown`, delete content,
   disable further attempts for that route/date, and stop. Reconciliation is
   manual in Messages; there is no retry button or automated retry.
8. **On boundary drift or leakage:** unexpected prompts, recipients, accounts,
   network destinations, personal diagnostics, raw automation source, or
   staging retention stop the complete experiment immediately. Preserve only
   a content-free failure code after cleanup.

## Redacted feasibility matrix

Every row begins with fixture adapters and `DraftOnlyTransport`. A row marked
"reviewed live" remains disabled until its named stop point is approved.
`not_configured` is recorded as `unsupported` with content-free context; the
experiment never creates an account to fill a matrix cell.

| Track | Scenario | Fixture or bounded stimulus | Permitted evidence | External effect gate |
|---|---|---|---|---|
| Calendar | Permission not requested, denied, granted, restricted, revoked | Synthetic selected-source set, then visible signed-app request | Permission state, count, outcome | Stop points 2–3; reads only |
| Calendar | Source selection | Synthetic iCloud-shaped and Google-shaped sources, then locally selected configured sources if present | Provider kind bucket, selected count, outcome | Stop point 3; no labels |
| Calendar | All-day, recurring, private, timezone boundary | Synthetic events across the accepted bounded window | Normalized shape booleans/counts | Fixture only until Step 3 |
| Contacts | Permission not requested, denied, granted, restricted, revoked | Synthetic partial birthdays and unified contacts, then visible signed-app request | Permission state, partial-date flags/counts | Stop points 2–3; reads only |
| Contacts | Source/account selection and change notification | Synthetic Apple/Google-shaped pairings; configured sources only when locally available | Provider kind bucket, change observed boolean | Stop point 3; no labels |
| Companion | Foreground launch and relaunch | Fixture providers and draft-only transport | Startup/recovery outcome, no content | No external effect |
| Companion | Login-item launch and app update | Fixture-only signed build | Identity-match boolean, launch outcome | Stop points 1 and 4 |
| Companion | Locked user session | Fixture request before/during/after lock | Availability/outcome transition | No automatic unlock or prompt |
| Companion | Mac sleep/wake | Fixture request around sleep and wake | Cancellation/recovery outcome | No wake scheduling |
| Companion | OS restart | Partial fixture staging and `dispatch_started` fixture | Cleanup count, `delivery_unknown`, zero replay | No live adapter |
| Messages | Sign-in and sender readiness | Content-free local status probe | Ready boolean or typed unavailable outcome | Stop point 5; no message |
| Messages | Direct Apple Events text | Approved synthetic marker to exact opaque self/test route | Attempt ordinal, transport, outcome | Reviewed live; stop points 5–6 |
| Messages | Fixed Shortcut text | Same marker and route, fixed pre-provisioned Shortcut | Attempt ordinal, transport, outcome | Reviewed live; stop points 5–6 |
| Messages | Direct Apple Events audio file | Approved generated tone after complete local playback | Duration/size, attachment-kind observation, outcome | Reviewed live; max two audio total |
| Messages | Fixed Shortcut audio file | Same generated tone and playback rule | Duration/size, attachment-kind observation, outcome | Reviewed live; max two audio total |
| Messages | Messages relaunch | One approved text attempt after local readiness returns | Outcome only | Reviewed live; one matrix attempt |
| Messages | Network loss and restoration | Force loss before handoff and separately after `dispatch_started` | `failed_before_submission` or `delivery_unknown` | Reviewed live; never retry |
| Messages | Mac sleep/wake during attempt | Sleep before handoff and separately after `dispatch_started` | Stable outcome and zero replay | Reviewed live; never retry |
| Messages | Permission revocation | Revoke Automation before handoff and during a bounded run | Permission/outcome only | Reviewed live; stop after observation |
| Messages | Adapter timeout/process crash | Inject before and after `dispatch_started` | Stable outcome, recovered single-use state | Fixture first; one reviewed live cell maximum |
| Messages | OS restart after handoff boundary | Recover durable fixture/live attempt state | `delivery_unknown`, content deleted, zero invocation | Fixture first; reviewed live only if separately named |
| Transcription | On-device supported/unsupported | Synthetic non-personal speech fixture | Tool/model/OS version, outcome, rounded timing | Local only; network capture must show no egress |
| Transcription | Cancellation, max duration, max size | Synthetic audio at each bound | Typed outcome, deletion count | Local only |
| Audio review | Complete playback, interruption, absent output | Generated tone through explicitly selected local output | Completion boolean, outcome | No Messages handoff |
| Tailnet | Loopback, exact grant, forged headers, replay, Funnel absence | Fake transport and signed synthetic requests | Capability/outcome booleans, packet count | Stop point 4; real adapter remains draft-only |

## Step 1 completion gate

No checkbox may be marked complete until human review explicitly confirms all
five of these clauses:

1. the accepted product defaults and continued `AGENDA-011` deferral;
2. the self/test recipient class;
3. the zero-by-default and 12-attempt/2-audio maximum external side effects;
4. the redaction and evidence allowlist; and
5. every manual stop point above.

Repository verification must then prove that this tracked experiment contains
no provider code, real identifier, personal content fixture, credential,
recipient route, audio, transcript, or live-adapter enablement.
