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
.claude/plans/2026-08-05-end-to-end-development-roadmap.md
.claude/plans/2026-08-05-beaglebone-hardware-baseline.md
.claude/plans/2026-08-05-lexinet-study-integration.md
.claude/plans/2026-08-05-personal-agenda-feasibility.md
docs/architecture/system-architecture.md
docs/architecture/application-capability-ownership.md
docs/architecture/power-domain-architecture.md
docs/architecture/process-interconnectivity.md
docs/architecture/lexinet-study-integration.md
docs/architecture/personal-agenda-and-birthday-wishes.md
docs/architecture/spoken-command-distinctiveness.md
docs/behavior/panel-policy.md
docs/behavior/flash-card-ux.md
docs/interfaces/lexinet-contract.md
docs/interfaces/panel-mode-protocol.md
docs/legacy/recovery-summary.md
docs/decisions/README.md
docs/decisions/0003-characterize-beaglebone-before-selecting-product-platform.md
docs/decisions/0006-accept-product-semantics-and-atomic-protocol-v2-cutover.md
docs/decisions/0007-validate-complete-protocol-v2-commitments.md
docs/decisions/0008-adopt-frdm-imx8mplus-and-waveshare-7-inch-dsi-development-platform.md
docs/operations/development-environment.md
docs/operations/build-and-deploy.md
docs/operations/board-bringup-provisioning.md
docs/operations/debian-tailnet-bringup.md
docs/operations/frdm-bringup.md
.claude/plans/2026-09-25-frdm-hardware-baseline.md
docs/research/gui-strategies.md
docs/research/sleep-wake-strategies.md
docs/research/compute-platform-assessment.md
docs/security/threat-model.md
docs/hardware/enclosure-plan.md
hardware_tests/README.md
experiments/macos_personal_companion/README.md
vocabulary_panel/README.md
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

require_fixed_count() {
  relative_path=$1
  required_text=$2
  expected_count=$3
  error_code=$4
  actual_count=$(rg --fixed-strings --only-matching -- "$required_text" \
    "$PROJECT_ROOT/$relative_path" | awk 'END { print NR + 0 }')
  if [ "$actual_count" -ne "$expected_count" ]; then
    bhp_fail "$error_code" \
      "$relative_path expected $expected_count occurrence(s) of $required_text; found $actual_count" \
      "$TRACE_ID"
    exit 1
  fi
}

require_exact_line_count() {
  relative_path=$1
  required_line=$2
  expected_count=$3
  error_code=$4
  actual_count=$(awk -v required_line="$required_line" '
    $0 == required_line { count += 1 }
    END { print count + 0 }
  ' "$PROJECT_ROOT/$relative_path")
  if [ "$actual_count" -ne "$expected_count" ]; then
    bhp_fail "$error_code" \
      "$relative_path expected $expected_count exact line(s) of $required_line; found $actual_count" \
      "$TRACE_ID"
    exit 1
  fi
}

require_decision_status() {
  relative_path=$1
  decision_id=$2
  required_status=$3
  awk -F '|' -v decision_id="$decision_id" \
    -v required_status="$required_status" '
    function trim(value) {
      sub(/^[[:space:]]+/, "", value)
      sub(/[[:space:]]+$/, "", value)
      return value
    }
    trim($2) == "`" decision_id "`" {
      decision_rows += 1
      if (trim($4) == required_status) {
        matching_rows += 1
      }
    }
    END { exit(decision_rows == 1 && matching_rows == 1 ? 0 : 1) }
  ' "$PROJECT_ROOT/$relative_path" || {
    bhp_fail VERIFY_DOCS.DECISION_STATUS_INVALID \
      "$decision_id must occur once with exact status $required_status in $relative_path" \
      "$TRACE_ID"
    exit 1
  }
}

require_accepted_decision() {
  require_decision_status "$1" "$2" 'Accepted — ADR 0006'
}

lexinet_control_pattern() {
  control_name=$1
  printf '(^|[^[:alnum:]_])%s[[:space:]]*(:)?[-=+?]' "$control_name"
}

require_single_tracked_lexinet_control() {
  control_name=$1
  expected_assignment=$2
  error_code=$3
  control_pattern=$(lexinet_control_pattern "$control_name")
  tracked_matches=$(git -C "$PROJECT_ROOT" grep -n -E \
    "$control_pattern" -- config scripts || true)
  tracked_match_count=$(printf '%s\n' "$tracked_matches" \
    | awk 'NF { count += 1 } END { print count + 0 }')
  if [ "$tracked_match_count" -ne 1 ]; then
    bhp_fail "$error_code" \
      "$control_name must have exactly one tracked assignment and no checked-in fallback; found $tracked_match_count" \
      "$TRACE_ID"
    exit 1
  fi

  matched_path=${tracked_matches%%:*}
  matched_remainder=${tracked_matches#*:}
  matched_line=${matched_remainder%%:*}
  matched_content=${matched_remainder#*:}
  case "$matched_line" in
    ''|*[!0-9]*)
      bhp_fail "$error_code" "$control_name match has no valid line number" \
        "$TRACE_ID"
      exit 1
      ;;
  esac
  if [ "$matched_path" != config/build-and-deploy.env.example ] || \
    [ "$matched_content" != "$expected_assignment" ]; then
    bhp_fail "$error_code" \
      "$control_name is allowed only as its literal build-and-deploy example assignment" \
      "$TRACE_ID"
    exit 1
  fi
}

require_control_pattern_count() {
  control_pattern=$1
  fixture_text=$2
  expected_count=$3
  fixture_name=$4
  actual_count=$(printf '%s\n' "$fixture_text" \
    | rg --only-matching "$control_pattern" | awk 'END { print NR + 0 }')
  if [ "$actual_count" -ne "$expected_count" ]; then
    bhp_fail VERIFY_DOCS.LEXINET_CONTROL_PATTERN_SELF_TEST_FAILED \
      "$fixture_name expected $expected_count prohibited control form(s); found $actual_count" \
      "$TRACE_ID"
    exit 1
  fi
}

lexinet_control_prefix=BHP_LEXINET_
lexinet_source_variable=${lexinet_control_prefix}SOURCE_DIR
lexinet_revision_variable=${lexinet_control_prefix}BASE_REVISION
lexinet_source_assignment=$lexinet_source_variable=/absolute/path/to/lexinet
lexinet_base_revision=3e15ef60e1970757e756d74622846edba166757c
lexinet_revision_assignment=$lexinet_revision_variable=$lexinet_base_revision
lexinet_source_control_pattern=$(lexinet_control_pattern \
  "$lexinet_source_variable")
lexinet_revision_control_pattern=$(lexinet_control_pattern \
  "$lexinet_revision_variable")

# In-memory negative fixtures keep the policy matcher honest without creating
# files or embedding a prohibited effective assignment in this script.
quoted_source_fixture=$(printf "export '%s=/forbidden/quoted-source'" \
  "$lexinet_source_variable")
default_assignment_fixture=$(printf "\${%s:=/forbidden/default-source}" \
  "$lexinet_source_variable")
default_fallback_fixture=$(printf "\${%s:-/forbidden/fallback-source}" \
  "$lexinet_source_variable")
noncolon_fallback_fixture=$(printf "\${%s-/forbidden/noncolon-fallback}" \
  "$lexinet_source_variable")
revision_override_fixture=$(printf '"%s=%s"' "$lexinet_revision_variable" \
  0000000000000000000000000000000000000000)
revision_noncolon_fallback_fixture=$(printf "\${%s-0000000000000000000000000000000000000000}" \
  "$lexinet_revision_variable")
duplicate_source_fixture=$(printf '%s\n%s' "$lexinet_source_assignment" \
  "$quoted_source_fixture")
require_control_pattern_count "$lexinet_source_control_pattern" \
  "$quoted_source_fixture" 1 quoted_assignment
require_control_pattern_count "$lexinet_source_control_pattern" \
  "$default_assignment_fixture" 1 parameter_default_assignment
require_control_pattern_count "$lexinet_source_control_pattern" \
  "$default_fallback_fixture" 1 parameter_default_fallback
require_control_pattern_count "$lexinet_source_control_pattern" \
  "$noncolon_fallback_fixture" 1 parameter_noncolon_fallback
require_control_pattern_count "$lexinet_source_control_pattern" \
  "$duplicate_source_fixture" 2 duplicate_assignment
require_control_pattern_count "$lexinet_revision_control_pattern" \
  "$revision_override_fixture" 1 revision_override
require_control_pattern_count "$lexinet_revision_control_pattern" \
  "$revision_noncolon_fallback_fixture" 1 revision_noncolon_fallback

require_single_tracked_lexinet_control "$lexinet_source_variable" \
  "$lexinet_source_assignment" VERIFY_DOCS.LEXINET_SOURCE_CONTROL_INVALID
require_single_tracked_lexinet_control "$lexinet_revision_variable" \
  "$lexinet_revision_assignment" VERIFY_DOCS.LEXINET_REVISION_CONTROL_INVALID

product_semantics_adr=docs/decisions/0006-accept-product-semantics-and-atomic-protocol-v2-cutover.md
require_exact_line_count "$product_semantics_adr" \
  '<!-- @canonical product-semantics-and-protocol-v2-cutover -->' 1 \
  VERIFY_DOCS.PRODUCT_SEMANTICS_ADR_CANONICAL_MARKER_INVALID
require_exact_line_count "$product_semantics_adr" \
  '# ADR 0006: Accept Product Semantics and Atomic Protocol v2 Cutover' 1 \
  VERIFY_DOCS.PRODUCT_SEMANTICS_ADR_TITLE_INVALID
require_fixed_count docs/decisions/README.md \
  '[0006](0006-accept-product-semantics-and-atomic-protocol-v2-cutover.md)' 1 \
  VERIFY_DOCS.PRODUCT_SEMANTICS_ADR_REGISTRY_ENTRY_INVALID

protocol_commitment_adr=docs/decisions/0007-validate-complete-protocol-v2-commitments.md
require_exact_line_count "$protocol_commitment_adr" \
  '<!-- @canonical complete-protocol-v2-commitment-validation -->' 1 \
  VERIFY_DOCS.PROTOCOL_COMMITMENT_ADR_CANONICAL_MARKER_INVALID
require_exact_line_count "$protocol_commitment_adr" \
  '# ADR 0007: Validate Complete Protocol v2 Commitments' 1 \
  VERIFY_DOCS.PROTOCOL_COMMITMENT_ADR_TITLE_INVALID
require_fixed_count docs/decisions/README.md \
  '[0007](0007-validate-complete-protocol-v2-commitments.md)' 1 \
  VERIFY_DOCS.PROTOCOL_COMMITMENT_ADR_REGISTRY_ENTRY_INVALID

development_platform_adr=docs/decisions/0008-adopt-frdm-imx8mplus-and-waveshare-7-inch-dsi-development-platform.md
require_exact_line_count "$development_platform_adr" \
  '<!-- @canonical development-platform-selection -->' 1 \
  VERIFY_DOCS.DEVELOPMENT_PLATFORM_ADR_CANONICAL_MARKER_INVALID
require_exact_line_count "$development_platform_adr" \
  '# ADR 0008: Adopt FRDM i.MX 8M Plus and Waveshare 7-inch DSI as the Development Platform' 1 \
  VERIFY_DOCS.DEVELOPMENT_PLATFORM_ADR_TITLE_INVALID
require_fixed_count docs/decisions/README.md \
  '[0008](0008-adopt-frdm-imx8mplus-and-waveshare-7-inch-dsi-development-platform.md)' 1 \
  VERIFY_DOCS.DEVELOPMENT_PLATFORM_ADR_REGISTRY_ENTRY_INVALID
for frdm_gate_id in \
  FRDM-GATE-BACKLIGHT FRDM-GATE-TOUCH FRDM-GATE-POWER FRDM-GATE-SUSPEND \
  FRDM-GATE-WIFI FRDM-GATE-PIR FRDM-GATE-IDENTITY FRDM-GATE-DISPLAY \
  FRDM-GATE-AUDIO; do
  require_fixed_count "$development_platform_adr" \
    "| \`$frdm_gate_id\` |" 1 VERIFY_DOCS.DEVELOPMENT_PLATFORM_GATE_ROW_INVALID
done

# The FRDM bring-up doc is agent-executable: every gate has a gate-map row,
# the checklist is numbered contiguously, and every script it names exists.
frdm_bringup=docs/operations/frdm-bringup.md
require_exact_line_count "$frdm_bringup" '<!-- @canonical frdm-bringup -->' 1 \
  VERIFY_DOCS.FRDM_BRINGUP_CANONICAL_MARKER_INVALID
for frdm_gate_id in \
  FRDM-GATE-BACKLIGHT FRDM-GATE-TOUCH FRDM-GATE-POWER FRDM-GATE-SUSPEND \
  FRDM-GATE-WIFI FRDM-GATE-PIR FRDM-GATE-IDENTITY FRDM-GATE-DISPLAY \
  FRDM-GATE-AUDIO; do
  require_fixed_count "$frdm_bringup" "| \`$frdm_gate_id\` |" 1 \
    VERIFY_DOCS.FRDM_BRINGUP_GATE_ROW_INVALID
done
python3 - "$PROJECT_ROOT" "$frdm_bringup" <<'PY_FRDM_BRINGUP' || {
import re
import sys
from pathlib import Path

root, document = Path(sys.argv[1]), sys.argv[2]
text = (root / document).read_text(encoding="utf-8")
steps = [int(n) for n in re.findall(r"^\| ARRIVAL-(\d{2}) \|", text, re.MULTILINE)]
if not steps or steps != list(range(1, len(steps) + 1)):
    print(f"checklist numbering {steps}")
    sys.exit(1)
names = set(re.findall(r"\b(frdm_[a-z0-9_]+\.sh)\b", text))
missing = [n for n in sorted(names)
           if not (root / "hardware_tests/frdm" / n).is_file() and not (root / "scripts/deploy" / n).is_file()]
paths = set(re.findall(r"\b(scripts/[a-z_]+/[a-z0-9_]+\.(?:sh|py))\b", text))
missing += [p for p in sorted(paths) if not (root / p).is_file()]
if missing:
    print("missing: " + " ".join(missing))
    sys.exit(1)
PY_FRDM_BRINGUP
  bhp_fail VERIFY_DOCS.FRDM_BRINGUP_CHECKLIST_INVALID "$frdm_bringup" "$TRACE_ID"
  exit 1
}

agenda_feasibility_contract=experiments/macos_personal_companion/README.md
require_exact_line_count "$agenda_feasibility_contract" \
  '<!-- @canonical macos-personal-companion-feasibility-contract -->' 1 \
  VERIFY_DOCS.AGENDA_FEASIBILITY_CANONICAL_MARKER_INVALID
require_exact_line_count "$agenda_feasibility_contract" \
  '**Status:** proposed for human safety review; no probe or external effect is' 1 \
  VERIFY_DOCS.AGENDA_FEASIBILITY_REVIEW_STATUS_INVALID
for agenda_contract_text in \
  "\`available\`" \
  "\`permission_denied\`" \
  "\`unsupported\`" \
  "\`failed_before_submission\`" \
  "\`submitted\`" \
  "\`delivery_unknown\`" \
  'Zero by default. One later explicit approval may authorize at most 12 total handoff attempts' \
  'never a feasibility target.' \
  'Audio-file submission remains disabled unless complete local playback succeeds;' \
  "Voice recognition may only open the reversible \`agenda.prepare_wish\` composer." \
  "set containing \`agenda.stop_wish_recording\`; the general command grammar cannot" \
  '## Redaction and evidence policy' \
  '## Manual stop points' \
  '## Step 1 completion gate'; do
  grep -F "$agenda_contract_text" "$PROJECT_ROOT/$agenda_feasibility_contract" \
    >/dev/null || {
    bhp_fail VERIFY_DOCS.AGENDA_FEASIBILITY_CONTRACT_INCOMPLETE \
      "$agenda_contract_text" "$TRACE_ID"
    exit 1
  }
done

agenda_experiment_root=$PROJECT_ROOT/experiments/macos_personal_companion
agenda_forbidden_artifact=$(find "$agenda_experiment_root" -type f \
  \( -name '*.wav' -o -name '*.m4a' -o -name '*.aac' -o -name '*.mp3' \
     -o -name '*.caf' -o -name '*.mobileprovision' -o -name '*.p12' \
     -o -name '*.cer' -o -name '*.xcuserstate' \) -print -quit)
if [ -n "$agenda_forbidden_artifact" ]; then
  bhp_fail VERIFY_DOCS.AGENDA_FEASIBILITY_PRIVATE_ARTIFACT_TRACKED \
    "$(basename "$agenda_forbidden_artifact")" "$TRACE_ID"
  exit 1
fi
if rg -n --hidden --glob '!README.md' \
  '(BEGIN (RSA |EC |OPENSSH )?PRIVATE KEY|Authorization:[[:space:]]*Bearer|[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,})' \
  "$agenda_experiment_root" >/dev/null; then
  bhp_fail VERIFY_DOCS.AGENDA_FEASIBILITY_IDENTIFIER_OR_SECRET_PRESENT \
    "Experiment source contains a credential or address-shaped value" "$TRACE_ID"
  exit 1
fi

require_exact_line_count docs/architecture/application-capability-ownership.md \
  '<!-- @canonical application-capability-ownership -->' 1 \
  VERIFY_DOCS.OWNERSHIP_CANONICAL_MARKER_INVALID
require_exact_line_count docs/architecture/application-capability-ownership.md \
  '**Status:** accepted by ADR 0006; implementation status remains per capability' 1 \
  VERIFY_DOCS.OWNERSHIP_REVIEW_STATUS_INVALID
require_fixed_count docs/architecture/application-capability-ownership.md \
  '## Registry' 1 VERIFY_DOCS.OWNERSHIP_REGISTRY_MISSING

ownership_registry=docs/architecture/application-capability-ownership.md
if ! ownership_registry_error=$(awk -F '|' \
  -v target_plan_token="roadmap Step 11's reviewed target-integration plan" '
  function trim(value) {
    sub(/^[[:space:]]+/, "", value)
    sub(/[[:space:]]+$/, "", value)
    return value
  }
  function fail(message) {
    if (failures == 0) {
      print message
    }
    failures += 1
  }
  function require_status(capability, expected_status) {
    if (row_count[capability] != 1 || row_status[capability] != expected_status) {
      fail(capability " must occur once with status " expected_status)
    }
  }
  function require_token(capability, required_text) {
    if (row_count[capability] != 1 || index(row_text[capability], required_text) == 0) {
      fail(capability " is missing row token " required_text)
    }
  }
  BEGIN {
    expected_header = "| Capability | Runtime authority / canonical location | First delivery authority | Implementation status | Extension rule |"
    absent_status = "Not implemented — declared path absent"
    supervisor = "Presence, solar, brightness, and suspend policy"
    lifecycle = "Supervisor/UI lifecycle contract"
    display = "Display, ~~touch,~~ application touch interpretation, LVGL lifetime, surface routing, and modal precedence"
    touch = "Physical-touch observation and single-use provenance"
    vocabulary = "Vocabulary presentation and study navigation"
    http = "Process-wide HTTP initialization, cancellation primitives, and common transport budgets"
    lexinet = "Vocabulary content, eligibility, session order, and durable learning state"
    media = "Raw microphone capture and purpose-scoped media leases"
    spoken = "Spoken-command vocabulary, pronunciation provenance, confusability analysis, active command sets, and backend recognizer projections"
    recognition = "Recognition decision policy and its content identity"
    personal_context = "Panel-to-Mac signing key and typed companion transport"
    mac_companion = "Calendar, Contacts, local dictation, recipient provisioning, and Messages handoff"
    civil_time = "Household civil-time projection for agenda and neutral clock display"
  }
  /^\| Capability \|/ {
    header_count += 1
    if ($0 != expected_header) {
      fail("ownership registry header does not contain the exact status column")
    }
    in_registry = 1
    next
  }
  in_registry && /^\|---/ { next }
  in_registry && /^\|/ {
    capability = trim($2)
    row_count[capability] += 1
    row_text[capability] = $0
    row_status[capability] = trim($5)
    registry_rows += 1
    if (row_count[capability] != 1 || NF != 7 || row_status[capability] == "") {
      fail("every capability must occur once with five nonempty table columns")
    }
    if (row_status[capability] == absent_status) {
      absent_status_rows += 1
    }
    next
  }
  in_registry { in_registry = 0 }
  END {
    if (header_count != 1 || registry_rows == 0 || absent_status_rows != 1) {
      fail("registry must have one status header and exactly one absent-path row")
    }
    require_status(http, "Host runtime implemented — process lifetime, cancellation, transport budgets, and worker joins tested")
    require_status(media, "Fake-only host broker implemented — real microphone capture and independent hardware quiescence evidence pending")
    require_status(spoken, "Host catalog and deterministic fake projection implemented — real recognizer selection and evidence pending")
    require_status(recognition, "Fake recognition policy validation implemented — real recognition policy evidence pending")
    require_status(personal_context, absent_status)
    require_token(display, "`panel_ui/`")
    require_token(display, "Neutral host shell")
    require_token(http, "`shared/http_runtime/`")
    require_token(media, "`media_broker/`")
    require_token(spoken, "`shared/spoken_command_catalog/`")
    require_token(spoken, "`docs/interfaces/spoken-command-catalog.json`")
    require_token(recognition, "`config/voice/recognition-policies/`")
    require_token(recognition, "`docs/interfaces/schemas/recognition-policy.schema.json`")
    require_token(recognition, "`media_broker/`")
    require_token(personal_context, "`personal_context_client/`")
    require_token(supervisor, "Lexinet plan Step 7a")
    require_token(lifecycle, "Lexinet plan Step 7a")
    require_token(display, "Lexinet plan Step 7a")
    require_token(vocabulary, "Lexinet plan Step 6 and Step 7b")
    require_token(lexinet, "Lexinet plan Step 1b, then Steps 2–5")
    require_token(touch, "Roadmap Step 2 / Lexinet plan Step 7a")
    require_token(touch, "roadmap Step 8 / Lexinet plan Step 9")
    if (index(row_text[touch], "after ADR 0005 approval") != 0) {
      fail("touch delivery still depends on already-accepted ADR 0005 approval")
    }
    require_token(media, "Roadmap Step 7")
    require_token(media, "separately reviewed roadmap Step 6 agenda production plan")
    require_token(personal_context, "Roadmap Step 7")
    require_token(personal_context, "separately reviewed roadmap Step 6 agenda production plan")
    require_token(mac_companion, "Personal-agenda feasibility plan selects adapters")
    require_token(mac_companion, "Roadmap Step 7")
    require_token(mac_companion, "separately reviewed roadmap Step 6 agenda production plan")
    require_status(civil_time, "Pending — no canonical implementation path selected")
    require_token(civil_time, "AGENDA-011 under roadmap Step 6")
    require_token(civil_time, target_plan_token)
    require_token(civil_time, "Step 12 execution")
    require_token(civil_time, "timezone arithmetic stays out of `panel_supervisor`")
    exit(failures == 0 ? 0 : 1)
  }
' "$PROJECT_ROOT/$ownership_registry"); then
  bhp_fail VERIFY_DOCS.OWNERSHIP_REGISTRY_INVALID \
    "$ownership_registry: $ownership_registry_error" "$TRACE_ID"
  exit 1
fi

for implemented_registry_path in shared/http_runtime media_broker \
  shared/spoken_command_catalog docs/interfaces/spoken-command-catalog.json \
  config/voice/recognition-policies docs/interfaces/schemas/recognition-policy.schema.json; do
  test -e "$PROJECT_ROOT/$implemented_registry_path" || {
    bhp_fail VERIFY_DOCS.OWNERSHIP_IMPLEMENTED_PATH_MISSING \
      "$implemented_registry_path is declared implemented but absent" "$TRACE_ID"
    exit 1
  }
done

absent_registry_path=personal_context_client
  test ! -e "$PROJECT_ROOT/$absent_registry_path" || {
    bhp_fail VERIFY_DOCS.OWNERSHIP_DECLARED_ABSENT_PATH_PRESENT \
      "$absent_registry_path exists but the ownership registry declares its path absent" \
      "$TRACE_ID"
    exit 1
  }

for decision_id in \
  LEXI-001 LEXI-002 LEXI-003 LEXI-004 LEXI-005 LEXI-006 LEXI-007; do
  require_accepted_decision docs/architecture/lexinet-study-integration.md \
    "$decision_id"
done
require_exact_line_count docs/architecture/lexinet-study-integration.md \
  '## Version 1 product decisions' 1 \
  VERIFY_DOCS.LEXINET_DECISION_REVIEW_MISSING

require_exact_line_count docs/architecture/spoken-command-distinctiveness.md \
  '<!-- @canonical spoken-command-distinctiveness -->' 1 \
  VERIFY_DOCS.SPOKEN_COMMAND_CANONICAL_MARKER_INVALID
require_exact_line_count docs/architecture/spoken-command-distinctiveness.md \
  '## Shared decision' 1 \
  VERIFY_DOCS.SPOKEN_COMMAND_DECISION_REVIEW_MISSING
require_accepted_decision docs/architecture/spoken-command-distinctiveness.md \
  VOICE-001
for command_design_phrase in \
  'phonetic distinctiveness' \
  'command confusability' \
  'send birthday wish' \
  'finish recording' \
  'ActiveCommandSet' \
  'active_command_set_digest' \
  'backend_artifact_digest' \
  'recognition_policy_digest' \
  'recognition-policy.schema.json' \
  'canonical JSON' \
  'spoken_form_id' \
  'bounded_learning_observation' \
  'capture_start' \
  'speakers are disjoint'; do
  rg --ignore-case --fixed-strings -- "$command_design_phrase" \
    "$PROJECT_ROOT/docs/architecture/spoken-command-distinctiveness.md" >/dev/null || {
    bhp_fail VERIFY_DOCS.SPOKEN_COMMAND_DESIGN_INCOMPLETE \
      "$command_design_phrase" "$TRACE_ID"
    exit 1
  }
done

for policy_contract in \
  docs/architecture/spoken-command-distinctiveness.md \
  docs/architecture/application-capability-ownership.md \
  docs/architecture/lexinet-study-integration.md \
  docs/security/threat-model.md \
  .claude/plans/2026-08-05-lexinet-study-integration.md \
  .claude/plans/2026-08-05-end-to-end-development-roadmap.md; do
  rg --ignore-case --fixed-strings -- 'recognition-policy artifact' \
    "$PROJECT_ROOT/$policy_contract" >/dev/null || {
    bhp_fail VERIFY_DOCS.RECOGNITION_POLICY_CONTRACT_INCOMPLETE \
      "$policy_contract" "$TRACE_ID"
    exit 1
  }
done

for effect_contract in \
  docs/architecture/spoken-command-distinctiveness.md \
  docs/architecture/lexinet-study-integration.md \
  docs/security/threat-model.md \
  .claude/plans/2026-08-05-lexinet-study-integration.md \
  .claude/plans/2026-08-05-end-to-end-development-roadmap.md; do
  rg --fixed-strings -- 'bounded_learning_observation' \
    "$PROJECT_ROOT/$effect_contract" >/dev/null || {
    bhp_fail VERIFY_DOCS.VOICE_EFFECT_CONTRACT_INCOMPLETE \
      "$effect_contract" "$TRACE_ID"
    exit 1
  }
done

require_voice_evidence_phrase() {
  evidence_contract=$1
  evidence_phrase=$2
  rg --ignore-case --fixed-strings -- "$evidence_phrase" \
    "$PROJECT_ROOT/$evidence_contract" >/dev/null || {
    bhp_fail VERIFY_DOCS.VOICE_EVIDENCE_CONTRACT_INCOMPLETE \
      "$evidence_contract is missing $evidence_phrase" "$TRACE_ID"
    exit 1
  }
}

require_voice_evidence_phrase \
  docs/architecture/spoken-command-distinctiveness.md \
  'single-use selection and untouched confirmation'
require_voice_evidence_phrase \
  docs/architecture/lexinet-study-integration.md \
  'after a single-use comparison manifest'
require_voice_evidence_phrase \
  docs/architecture/lexinet-study-integration.md \
  'artifact/policy passes an untouched final-confirmation manifest'
require_voice_evidence_phrase docs/security/threat-model.md \
  'single-use held-out selection plus untouched confirmation evidence'
require_voice_evidence_phrase \
  .claude/plans/2026-08-05-lexinet-study-integration.md \
  'single-use comparison/selection'
require_voice_evidence_phrase \
  .claude/plans/2026-08-05-lexinet-study-integration.md \
  'untouched final-confirmation manifests'
require_voice_evidence_phrase \
  .claude/plans/2026-08-05-end-to-end-development-roadmap.md \
  'single-use selection, and untouched confirmation splits'

for lexinet_rating_phrase in \
  'variety_first_v1' \
  'rating_voided' \
  'equivalent touch and voice intents' \
  'per-profile ingestion ordinal' \
  'one durable Undo lease' \
  'study.rating.undo' \
  'client_stream_id' \
  'study_event_receipts' \
  'boundary_repeat_due_to_priority' \
  'predecessor flush receipt' \
  'STUDY_CYCLE_NOT_EXHAUSTED' \
  'waiting_for_corpus' \
  'server-authoritative exhaustion' \
  'completed predecessors'; do
  rg --ignore-case --fixed-strings -- "$lexinet_rating_phrase" \
    "$PROJECT_ROOT/docs/architecture/lexinet-study-integration.md" >/dev/null || {
    bhp_fail VERIFY_DOCS.LEXINET_RATING_SCHEDULER_DESIGN_INCOMPLETE \
      "$lexinet_rating_phrase" "$TRACE_ID"
    exit 1
  }
done

for agenda_intent_id in agenda.prepare_wish agenda.stop_wish_recording; do
  for agenda_contract in \
    docs/architecture/spoken-command-distinctiveness.md \
    docs/architecture/personal-agenda-and-birthday-wishes.md; do
    rg --fixed-strings -- "$agenda_intent_id" \
      "$PROJECT_ROOT/$agenda_contract" >/dev/null || {
      bhp_fail VERIFY_DOCS.AGENDA_INTENT_ID_MISSING \
        "$agenda_contract is missing $agenda_intent_id" "$TRACE_ID"
      exit 1
    }
  done
done

if rg --fixed-strings -- 'birthday_wish.prepare' "$PROJECT_ROOT/docs" \
  "$PROJECT_ROOT/.claude/plans" >/dev/null; then
  bhp_fail VERIFY_DOCS.STALE_AGENDA_INTENT_ID \
    'birthday_wish.prepare must use the canonical agenda.prepare_wish ID' \
    "$TRACE_ID"
  exit 1
fi

for obsolete_voice_contract in \
  'same three-command corpus' \
  "grammar matching for \`Lexi (flip|next|back)\`"; do
  if rg --fixed-strings -- "$obsolete_voice_contract" "$PROJECT_ROOT/docs" \
    "$PROJECT_ROOT/.claude/plans" >/dev/null; then
    bhp_fail VERIFY_DOCS.OBSOLETE_VOICE_CONTRACT_ACTIVE \
      "$obsolete_voice_contract" "$TRACE_ID"
    exit 1
  fi
done

for stale_study_contract in \
  'undo_rating' \
  'derived-through event sequence' \
  'input-event high-water ordinal' \
  'per-profile event high-water ordinal' \
  'per-profile event ordinal'; do
  if rg --fixed-strings -- "$stale_study_contract" "$PROJECT_ROOT/docs" \
    "$PROJECT_ROOT/.claude/plans" >/dev/null; then
    bhp_fail VERIFY_DOCS.STALE_STUDY_CONTRACT_ACTIVE \
      "$stale_study_contract" "$TRACE_ID"
    exit 1
  fi
done

rg --fixed-strings -- 'subordinate render' \
  "$PROJECT_ROOT/vocabulary_panel/README.md" >/dev/null || {
  bhp_fail VERIFY_DOCS.VOCABULARY_OWNER_STALE \
    'vocabulary_panel must remain a subordinate render surface' "$TRACE_ID"
  exit 1
}

for decision_id in \
  AGENDA-001 AGENDA-002 AGENDA-003 AGENDA-004 AGENDA-005 \
  AGENDA-006 AGENDA-007 AGENDA-008 AGENDA-009 AGENDA-010; do
  require_accepted_decision \
    docs/architecture/personal-agenda-and-birthday-wishes.md "$decision_id"
done
require_decision_status \
  docs/architecture/personal-agenda-and-birthday-wishes.md AGENDA-011 \
  'Pending — deliberately deferred to roadmap Step 6 after feasibility evidence and its ADR'
require_exact_line_count docs/architecture/personal-agenda-and-birthday-wishes.md \
  '## Version 1 product decisions' 1 \
  VERIFY_DOCS.AGENDA_DECISION_REVIEW_MISSING
require_fixed_count docs/architecture/personal-agenda-and-birthday-wishes.md \
  '### Safety stop points included in this review' 1 \
  VERIFY_DOCS.AGENDA_SAFETY_REVIEW_MISSING
require_fixed_count docs/architecture/personal-agenda-and-birthday-wishes.md \
  'No live Messages test is authorized' 1 \
  VERIFY_DOCS.AGENDA_LIVE_TEST_STOP_MISSING
require_fixed_count docs/architecture/personal-agenda-and-birthday-wishes.md \
  'Approving the touch-authority safety boundary accepts' 1 \
  VERIFY_DOCS.AGENDA_UI_COMPROMISE_DECISION_MISSING
require_fixed_count docs/architecture/personal-agenda-and-birthday-wishes.md \
  '30-minute hard ceiling' 1 \
  VERIFY_DOCS.AGENDA_RETENTION_DECISION_MISSING

for protocol_binding in \
  appliance_release_id supervisor_session_id connection_challenge snapshot_id \
  render_generation; do
  protocol_binding_row=$(printf "| \`%s\` |" "$protocol_binding")
  require_fixed_count docs/interfaces/panel-mode-protocol.md \
    "$protocol_binding_row" 1 VERIFY_DOCS.PROTOCOL_V2_BINDING_INVALID
done
require_fixed_count docs/interfaces/panel-mode-protocol.md \
  'atomic appliance-release cutover' 1 \
  VERIFY_DOCS.PROTOCOL_V2_MIGRATION_DECISION_MISSING

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
  "delivery_unknown" \
  "Mac companion" \
  "BHP_LEXINET_SOURCE_DIR" \
  "3D"; do
  rg --ignore-case --fixed-strings --glob '*.md' "$required_phrase" "$PROJECT_ROOT/README.md" "$PROJECT_ROOT/docs" >/dev/null || {
    bhp_fail VERIFY_DOCS.INTENT_MISSING "$required_phrase" "$TRACE_ID"
    exit 1
  }
done

bhp_log info documentation_verification_complete "$TRACE_ID" mermaid_diagram_count "$mermaid_count"
