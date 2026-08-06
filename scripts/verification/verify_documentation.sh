#!/bin/sh
# Checks the durable project narrative and required diagrams are present.
# Inputs: none. Side effects: none. Idempotent.

set -eu

SCRIPT_DIRECTORY=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
PROJECT_ROOT=$(CDPATH='' cd -- "$SCRIPT_DIRECTORY/../.." && pwd)
# shellcheck source=../structured_logging/structured_log.sh
. "$PROJECT_ROOT/scripts/structured_logging/structured_log.sh"
BHP_SCRIPT_SERVICE_NAME=beaglebone_house_panel.verify_documentation
export BHP_SCRIPT_SERVICE_NAME
TRACE_ID=$(bhp_new_trace_id)

required_documents="
README.md
.claude/plans/2026-08-05-beaglebone-hardware-baseline.md
docs/architecture/system-architecture.md
docs/architecture/power-domain-architecture.md
docs/architecture/process-interconnectivity.md
docs/behavior/panel-policy.md
docs/behavior/flash-card-ux.md
docs/interfaces/lexinet-contract.md
docs/interfaces/panel-mode-protocol.md
docs/legacy/recovery-summary.md
docs/decisions/0003-characterize-beaglebone-before-selecting-product-platform.md
docs/operations/development-environment.md
docs/operations/build-and-deploy.md
docs/operations/board-bringup-provisioning.md
docs/research/gui-strategies.md
docs/research/sleep-wake-strategies.md
docs/research/compute-platform-assessment.md
docs/security/threat-model.md
docs/hardware/enclosure-plan.md
hardware_tests/README.md
"

for relative_path in $required_documents; do
  test -s "$PROJECT_ROOT/$relative_path" || {
    bhp_fail VERIFY_DOCS.REQUIRED_DOCUMENT_MISSING "$relative_path" "$TRACE_ID"
    exit 1
  }
done

command -v rg >/dev/null 2>&1 || {
  bhp_fail VERIFY_DOCS.RIPGREP_MISSING "ripgrep is required" "$TRACE_ID"
  exit 1
}

mermaid_count=$(rg --glob '*.md' --count-matches '^```mermaid$' "$PROJECT_ROOT/README.md" "$PROJECT_ROOT/docs" \
  | awk -F: '{ total += $NF } END { print total + 0 }')
if [ "$mermaid_count" -lt 8 ]; then
  bhp_fail VERIFY_DOCS.DIAGRAMS_MISSING "Expected at least 8 Mermaid diagrams; found $mermaid_count" "$TRACE_ID"
  exit 1
fi

for required_phrase in \
  "not a hardware-complete product" \
  "PROVISIONAL" \
  "LVGL" \
  "gpio-keys" \
  "3D"; do
  rg --ignore-case --fixed-strings --glob '*.md' "$required_phrase" "$PROJECT_ROOT/README.md" "$PROJECT_ROOT/docs" >/dev/null || {
    bhp_fail VERIFY_DOCS.INTENT_MISSING "$required_phrase" "$TRACE_ID"
    exit 1
  }
done

bhp_log info documentation_verification_complete "$TRACE_ID" mermaid_diagram_count "$mermaid_count"
