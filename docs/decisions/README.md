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
| [0003](0003-characterize-beaglebone-before-selecting-product-platform.md) | Accepted for the current investigation | Characterize BeagleBone before selecting the product platform |
| [0004](0004-freeze-review-gate-integration-contracts.md) | Accepted | Freeze review-gate integration contracts |
| [0005](0005-validate-touch-provenance-at-capability-holders.md) | Accepted | Validate touch provenance at capability holders |
| [0006](0006-accept-product-semantics-and-atomic-protocol-v2-cutover.md) | Accepted | Accept product semantics and atomic protocol-v2 cutover |

No later number is reserved by a plan.

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
