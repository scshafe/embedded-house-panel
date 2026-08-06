# Plan: Document Debian Tailnet bring-up

**Goal:** Record the current BeagleBone Black/Tailscale compatibility evidence
and a safe, testable official-Debian-to-Lexinet bring-up route.
**Out of scope:** Installing software on hardware, changing tailnet policy,
provisioning credentials, or updating the Buildroot Tailscale package.

## Context

The architecture already routes the vocabulary client to Lexinet through
`tailscaled`, and the Buildroot external tree already selects Tailscale. The
missing artifact is an operator-facing procedure for validating that route on
the current official BeagleBoard Debian image before reproducing it in the
custom image. Repository policy forbids secrets in Git, command arguments,
URLs, logs, image overlays, and diagnostics.

## Steps

- [x] **Step 1: Add the Debian Tailnet compatibility and bring-up runbook**
  - Verify: `test -s docs/operations/debian-tailnet-bringup.md`

- [x] **Step 2: Make the runbook discoverable and required**
  - Verify: `rg --fixed-strings 'debian-tailnet-bringup.md' README.md docs/operations/board-bringup-provisioning.md scripts/verification/verify_documentation.sh`

- [x] **Step 3: Review and verify the documentation change**
  - Verify: `make verify && git diff --check`

- [x] **Step 4: Commit the completed documentation scope**
  - Verify: `git show --stat --oneline --summary HEAD`

## Done criteria

The compatibility claim, constraints, provisioning boundary, validation
sequence, suspend/resume measurements, and Buildroot handoff are documented;
repository verification passes; and the scoped changes are committed with a
`docs:` subject.
