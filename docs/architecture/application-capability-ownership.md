<!-- @canonical application-capability-ownership -->

# Application Capability Ownership

**Status:** proposed for roadmap Step 1 human review

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

| Capability | Runtime authority / canonical location | First delivery authority | Extension rule |
|---|---|---|---|
| Presence state and Linux wake event | `presence_sensor/` | BeagleBone hardware baseline plan | Implementations emit the same persistent input ABI; no presence implementation owns product policy |
| Presence, solar, brightness, and suspend policy | `panel_supervisor/` | Implemented scaffold; protocol-v2 work continues in the Lexinet plan's Step 7 | No UI, feature, broker, or kernel module may write policy or power state |
| Supervisor/UI lifecycle contract | `docs/interfaces/panel-mode-protocol.md`; supervisor publishes and `panel_ui` acknowledges | Lexinet plan Step 7 | Version the one contract; never introduce a feature-specific lifecycle channel |
| Display, ~~touch,~~ application touch interpretation, LVGL lifetime, surface routing, and modal precedence | planned `panel_ui/` | Lexinet plan Step 7 | New features add a reducer/surface and namespaced intents beneath the one shell; the UI consumes normalized touch observations but cannot author touch provenance |
| Physical-touch observation and single-use provenance | planned narrow capability inside `panel_supervisor/`, governed by [ADR 0005](../decisions/0005-validate-touch-provenance-at-capability-holders.md) | A reviewed implementation plan after ADR 0005 approval | The supervisor is the only direct touch-evdev reader and provenance issuer; cutover revokes raw UI access, forwards normalized observations including observer-measured contact duration without interpreting feature semantics, and admits the broker and personal-context client under distinct exact service UIDs to claim one observation for one operation |
| Vocabulary presentation and study navigation | `vocabulary_panel/` as a subordinate feature domain | Lexinet plan Steps 6–7 | Extend the study reducer; do not restore a vocabulary-owned display executable |
| Personal-agenda presentation and birthday-wish composer | planned agenda feature beneath `panel_ui` | Separate post-feasibility production plan | Extend the shell and media contracts selected by the earlier Lexinet steps |
| Process-wide HTTP initialization, cancellation primitives, and common transport budgets | planned `shared/http_runtime/` | Lexinet plan Step 6 | Domain clients retain their schemas, authorization, cache, and retry semantics while sharing one runtime |
| Vocabulary content, eligibility, session order, and durable learning state | Lexinet server in the separately configured source checkout | Lexinet plan Steps 1–5 | The panel consumes Lexinet-owned versioned contracts and keeps only bounded degraded state |
| Vocabulary cache and idempotent event outbox | `vocabulary_panel/` | Lexinet plan Step 6 | Persistence rules remain study-specific; do not hide them in a generic store |
| Raw microphone capture and purpose-scoped media leases | planned shared `media_broker/` process | Lexinet plan Step 8 | Add purposes to the one broker; `command_recognition` and `wish_recording` never become separate device owners |
| Spoken-command vocabulary, pronunciation provenance, confusability analysis, active command sets, and backend recognizer projections | planned `shared/spoken_command_catalog/`, governed by `docs/interfaces/spoken-command-catalog.json` | Lexinet plan Step 8 | Every application registers namespaced semantic intents in the one versioned catalog; no feature owns a private phrase list or handwritten peer recognizer configuration |
| Recognition decision policy and its content identity | validated artifacts under `config/voice/recognition-policies/`, governed by `docs/interfaces/schemas/recognition-policy.schema.json`; `media_broker/` verifies the selected artifact at runtime | Lexinet plan Step 8 | The shared builder canonically serializes and binds every policy to its backend artifact and recognizer build/model; callers never assert a free-standing digest or bypass policy validation |
| Panel-to-Mac signing key and typed companion transport | planned `personal_context_client/` | Post-feasibility agenda production plan | UI and broker call typed capabilities and never receive the private key or a general companion socket |
| Calendar, Contacts, local dictation, recipient provisioning, and Messages handoff | signed per-user Mac companion | Personal-agenda feasibility plan selects adapters; later production plan implements them | Providers implement the documented extension points; the panel never receives provider credentials or raw recipient handles |
| Buildroot package/service migration | `system_image/` | Lexinet plan Step 9 creates the neutral shell/broker layout | Agenda packaging extends those packages, users, groups, sockets, and state paths |

## Shared-boundary consequences

- The Lexinet plan may create `panel_ui`, `shared/http_runtime`, and the media
  broker because it reaches them first. Their names, interfaces, permissions,
  and tests must be panel-wide from their first commit.
- The personal-agenda feasibility plan stops at evidence and an ADR. Its later
  production plan consumes the existing shell/runtime/broker instead of
  creating agenda-owned equivalents.
- The supervisor exposes lifecycle and generic media-safety facts only. It does
  not learn feature routes, birthdays, vocabulary sessions, recording content,
  recipient identity, or provider state.
- The supervisor's proposed touch-provenance capability observes and attests a
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
  suspend. A failure or quiescence timeout fails the media purpose closed and
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
security-relevant physical-touch observation. Proposed ADR 0005 assigns the
latter to a narrow supervisor capability so every effect holder can validate
provenance without trusting `panel_ui`; no roadmap or pending product decision
is approved by this amendment.
