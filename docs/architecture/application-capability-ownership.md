<!-- @canonical application-capability-ownership -->

# Application Capability Ownership

**Status:** accepted by ADR 0006; implementation status remains per capability

This registry is the authority for which runtime component owns each shared
house-panel capability and which executable plan first delivers it. A plan may
introduce a neutral capability because it needs the first implementation; that
does not make the capability feature-specific. Later work extends the canonical
implementation instead of creating a peer.

## Ownership rules

- One runtime authority owns every privileged device or irreversible effect.
- One implementation and one location own every shared software capability.
- Feature reducers are siblings beneath `panel_ui`; neither vocabulary nor
  agenda owns the appliance shell.
- Delivery-plan ownership is temporary sequencing. Runtime ownership is the
  lasting architecture.
- A planned capability remains disabled until its implementation and evidence
  gate pass. Naming an owner does not claim that the capability exists.

## Registry

| Capability | Runtime authority / canonical location | First delivery authority | Implementation status | Extension rule |
|---|---|---|---|---|
| ~~Presence state and Linux wake event~~ | ~~`presence_sensor/`~~ | ~~BeagleBone hardware baseline plan~~ | ~~Prototype implementations present — target compilation and wake evidence pending~~ | ~~Implementations emit the same persistent input ABI; no presence implementation owns product policy~~ |
| Presence state and Linux wake event | `presence_sensor/` | BeagleBone hardware baseline plan | Prototype implementations present — target compilation and wake evidence pending | Implementations emit the same persistent input ABI and report wake capability only; no presence implementation chooses or applies product policy |
| ~~Presence, solar, brightness, and suspend policy~~ | ~~`panel_supervisor/`~~ | ~~Implemented scaffold; protocol-v2 work continues in Lexinet plan Step 7a~~ | ~~Implemented host scaffold — target hardware evidence pending~~ | ~~No UI, feature, broker, or kernel module may write policy or power state~~ |
| Presence, solar, brightness, and suspend policy | `panel_supervisor/` | Implemented scaffold; protocol-v2 work continues in Lexinet plan Step 7a; evidence-selected PIR-policy work continues in later roadmap steps | Host scaffold implemented; PIR debounce/wake-rate controls and target hardware evidence pending | This authority includes PIR debounce and attributed wake-rate policy. No UI, feature, broker, kernel module, or presence provider may choose policy values or write power state; production values have no unmeasured defaults |
| Supervisor/UI lifecycle contract | `docs/interfaces/panel-mode-protocol.md`; supervisor publishes and `panel_ui` acknowledges | Lexinet plan Step 7a | Protocol v2 codec and duplex host peers implemented; target display-commit and hardware evidence pending | Version the one contract; never introduce a feature-specific lifecycle channel |
| Display, ~~touch,~~ application touch interpretation, LVGL lifetime, surface routing, and modal precedence | `panel_ui/` | Lexinet plan Step 7a | Neutral host shell, intent ingress, headless renderer, and authenticated lifecycle client implemented; LVGL/display/touch target adapters and target evidence pending | New features add a reducer/surface and namespaced intents beneath the one shell; the UI consumes normalized touch observations but cannot author touch provenance |
| Physical-touch observation and single-use provenance | narrow authority inside `panel_supervisor/` plus the process-neutral record and queue in `shared/touch_observation_protocol/`, governed by [ADR 0005](../decisions/0005-validate-touch-provenance-at-capability-holders.md) | Roadmap Step 2 / Lexinet plan Step 7a for host implementation; roadmap Step 8 / Lexinet plan Step 9 for the packaging cutover | Host single-use provenance authority and bounded nonblocking normalized forwarding implemented; raw-device/IPC adapters, permissions cutover, and target evidence pending | The supervisor is the only direct touch-evdev reader and provenance issuer; cutover revokes raw UI access, forwards normalized observations including observer-measured contact duration without interpreting feature semantics, and admits the broker and personal-context client under distinct exact service UIDs to claim one observation for one operation |
| Vocabulary presentation and study navigation | `vocabulary_panel/` as a subordinate feature domain | Lexinet plan Step 6 and Step 7b | Partially implemented — subordinate feature and LVGL renderer exist; neutral-shell integration pending | Extend the study reducer; do not restore a vocabulary-owned display executable |
| Personal-agenda presentation and birthday-wish composer | planned agenda feature beneath `panel_ui` | Roadmap Step 7, executing the separately reviewed roadmap Step 6 agenda production plan | Not implemented — roadmap Step 6 planning and Step 7 execution pending | Extend the shell and media contracts selected by the earlier Lexinet steps |
| Household civil-time projection for agenda and neutral clock display | planned shared household civil-time projection; canonical path not selected | AGENDA-011 under roadmap Step 6; conditional target packaging and validation follow roadmap Step 11's reviewed target-integration plan and Step 12 execution | Pending — no canonical implementation path selected | Agenda and neutral clock display consume the one projection; timezone arithmetic stays out of `panel_supervisor` and never affects solar, brightness, or suspend policy |
| Process-wide HTTP initialization, cancellation primitives, and common transport budgets | planned `shared/http_runtime/` | Lexinet plan Step 6 | Not implemented — declared path absent | Domain clients retain their schemas, authorization, cache, and retry semantics while sharing one runtime |
| Vocabulary content, eligibility, session order, and durable learning state | Lexinet server in the source checkout selected only by `BHP_LEXINET_SOURCE_DIR` | Lexinet plan Step 1b, then Steps 2–5 | External `study.v1` authority and panel fixtures, persistence/event/merge/successor storage, scheduler/projector/materializer, canonical service/routes/CLI, transitional operator authorization, and post-commit-only shadow composition are implemented; final scoped Tailnet authorization remains pending | The panel consumes Lexinet-owned versioned contracts and keeps only bounded degraded state; no tool infers a sibling checkout |
| Vocabulary cache and idempotent event outbox | `vocabulary_panel/` | Lexinet plan Step 6 | Not implemented — the current vocabulary scaffold has no resilient cache or outbox | Persistence rules remain study-specific; do not hide them in a generic store |
| ~~Raw microphone capture and purpose-scoped media leases~~ | ~~planned shared `media_broker/` process~~ | ~~Lexinet plan Step 8 first delivers the host broker; Roadmap Step 7 extends it for agenda by executing the separately reviewed roadmap Step 6 agenda production plan~~ | ~~Not implemented — declared path absent~~ | ~~Add purposes to the one broker; `command_recognition` and `wish_recording` never become separate device owners~~ |
| Raw microphone capture and purpose-scoped media leases | planned shared `media_broker/` process; `panel_supervisor/` independently verifies quiescence at the lifecycle boundary | Lexinet plan Step 8 first delivers the host broker; Roadmap Step 7 extends it for agenda by executing the separately reviewed roadmap Step 6 agenda production plan | Not implemented — declared path absent | Add purposes to the one broker; `command_recognition` and `wish_recording` never become separate device owners; broker closure self-report is telemetry; lifecycle blanking is immediate, then the supervisor requires bounded independent kernel/device and available rail-off evidence before the suspend-state write |
| Spoken-command vocabulary, pronunciation provenance, confusability analysis, active command sets, and backend recognizer projections | planned `shared/spoken_command_catalog/`, governed by `docs/interfaces/spoken-command-catalog.json` | Lexinet plan Step 8 | Not implemented — declared path absent | Every application registers namespaced semantic intents in the one versioned catalog; no feature owns a private phrase list or handwritten peer recognizer configuration |
| Recognition decision policy and its content identity | validated artifacts under `config/voice/recognition-policies/`, governed by `docs/interfaces/schemas/recognition-policy.schema.json`; `media_broker/` verifies the selected artifact at runtime | Lexinet plan Step 8 | Not implemented — declared path absent | The shared builder canonically serializes and binds every policy to its backend artifact and recognizer build/model; callers never assert a free-standing digest or bypass policy validation |
| Panel-to-Mac signing key and typed companion transport | planned `personal_context_client/` | Roadmap Step 7, executing the separately reviewed roadmap Step 6 agenda production plan | Not implemented — declared path absent | UI and broker call typed capabilities and never receive the private key or a general companion socket |
| Calendar, Contacts, local dictation, recipient provisioning, and Messages handoff | signed per-user Mac companion | Personal-agenda feasibility plan selects adapters; Roadmap Step 7 implements the reviewed slice by executing the separately reviewed roadmap Step 6 agenda production plan | Not implemented — feasibility evidence and adapter selection pending | Providers implement the documented extension points; the panel never receives provider credentials or raw recipient handles |
| Buildroot package/service migration | `system_image/` | Lexinet plan Step 9 completes the neutral shell/broker layout | Neutral `bhp-panel-ui` package/principal entry added; complete service, device-permission, and broker migration pending | Agenda packaging extends those packages, users, groups, sockets, and state paths |

## Shared-boundary consequences

- The Lexinet plan is the accepted first delivery authority for `panel_ui`,
  `shared/http_runtime`, the media broker, and the Buildroot neutral-service
  migration because it reaches them first. Their names, interfaces,
  permissions, and tests must be panel-wide from their first commit.
- The personal-agenda feasibility plan stops at evidence and an ADR. Its later
  production plan consumes the existing shell/runtime/broker instead of
  creating agenda-owned equivalents. ADR 0006 does not complete that plan's
  Step 1 experiment-contract review or authorize a live external effect.
- The supervisor exposes lifecycle and generic media-safety facts only. It does
  not learn feature routes, birthdays, vocabulary sessions, recording content,
  recipient identity, or provider state.
- The supervisor owns evidence-selected PIR debounce and attributed wake-rate
  policy. The presence provider remains evidence-only. On the Black, supervisor
  filtering begins after resume and cannot undo the kernel edge's already-spent
  wake energy.
- The supervisor's touch-provenance capability observes and attests a
  completed physical contact without deciding what the touch means. The UI
  cannot claim an observation from the supervisor; the broker or
  personal-context client binds it to one opaque operation digest after
  validating the request.
- The existing vocabulary service's raw `bhp-touch` access is scaffold state,
  not the proposed boundary. The later Buildroot cutover must remove that group
  membership and raw node permission from the UI when the supervisor-owned
  observation stream is enabled; this documentation amendment does not change
  packaging.
- The media broker owns raw capture but not display, message submission, or
  suspend. ~~A failure or quiescence timeout fails the media purpose closed and
  cannot hold power policy hostage.~~ Its closure report is telemetry. The
  supervisor blanks immediately,
  then independently verifies kernel-visible capture-owner/device closure and
  available rail-off evidence before the suspend-state write; a failure or
  quiescence timeout takes the bounded broker-termination/media-off path and
  cannot hold power policy hostage.
- The media broker consumes only the content-addressed backend projection for
  its canonical active command set and its matching validated,
  content-addressed recognition-policy artifact.
  Catalog analysis helps reviewers choose distinguishable phrases but never
  authorizes an intent or substitutes for measured recognizer behavior.
- Buildroot service ownership follows these runtime boundaries. Convenience
  groups such as generic `input` or `audio` never replace exact device
  classification.

## Change procedure

Changing an owner requires an ADR because it moves a trust boundary and alters
which process can exercise a device, secret, or external effect. Adding a new
implementation behind an existing documented extension point does not require
an ownership ADR, but it must retain the same contract, permissions, structured
failures, and evidence gates.

**Amended 2026-08-07:** split application touch interpretation from
security-relevant physical-touch observation. ~~Proposed ADR 0005~~ Accepted
ADR 0005 assigns the latter to a narrow supervisor capability so every effect
holder can validate provenance without trusting `panel_ui`; no roadmap or
pending product decision is approved by this amendment.

**Amended 2026-08-07:** recorded implementation status separately from planned
ownership, reconciled the accepted ADR 0005 delivery sequence, and registered
the still-undecided household civil-time projection without assigning timezone
arithmetic to the supervisor.

**Amended 2026-08-08:** registered supervisor-owned, evidence-selected PIR
debounce/attributed wake-rate policy and supervisor-verifiable broker
quiescence. The presence provider remains evidence-only, broker self-report is
telemetry, and no unmeasured hardware value became a default.

**Amended 2026-08-16:** ADR 0006 accepted this registry and assigned the Lexinet
work stream first delivery of the neutral shell, shared HTTP runtime, media
broker, and Buildroot migration. This freezes ownership and extension rules,
not implementation or hardware status; agenda feasibility Step 1 remains open.

**Amended 2026-08-17:** recorded completion of Lexinet's Step 2 durable study
storage boundary without claiming the pending scheduler, public service API,
final scoped authorization, resilient panel client, or target integration.

**Amended 2026-08-17:** recorded completion of Lexinet's Step 3 scheduling
boundary at revision `517da8a3823fbaa0d70018831be75ed734c2f96d`. The scheduler,
projectors, cycle materializer, and authority-isolated shadow-runner seam are
implemented; public service/routes/CLI, production post-commit shadow
composition, final scoped authorization, resilient panel client, and target
integration remain pending. No capability owner or accepted decision changed.

**Amended 2026-08-17:** recorded Lexinet Step 4 completion at revision
`e8a9d52e35bdd55aa7451f4413c781f8371ebb8f`. The canonical service, strict
study routes, bounded operator CLI/client, transitional operator authorization,
and post-commit-only shadow composition are implemented and evidenced. Final
scoped Tailnet authorization, the resilient panel client, packaging, and target
integration remain pending; no capability owner or accepted decision changed.
