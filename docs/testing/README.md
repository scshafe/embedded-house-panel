# Testing and verification traceability

The [plan-step verification map](plan-step-verification-map.json) is the
canonical index from each active plan `Verify:` clause item to one unique test
symbol and the command or procedure that owns its proof.

## Active-item rule

`scripts/verification/check_plan_verification_map.py` discovers every
`Verify:` block in every Markdown plan below `.claude/plans/`. A completed
checkbox does not retire its verification obligation. Only text enclosed in
the repository's `~~strikethrough~~` amendment notation is inactive.

The checker also accepts the existing `- Verify <claim>` form without a colon.
Any bullet beginning with `Verify` that matches neither supported form fails as
`VERIFY_SYNTAX_INVALID`; verification-looking text cannot disappear silently.

The checker normally treats each semicolon-delimited statement as one clause
item. It deliberately decomposes Lexinet Steps 4 and 6 further because their
long behavior lists would otherwise map dozens of independent claims to an
umbrella symbol. Inline-code semicolons are not delimiters.

Each JSON entry has:

- `plan`, `step`, `verify_index`, and `item_index`: the stable lookup key;
- `clause_item`: the exact normalized active text, so plan drift fails closed;
- `test_symbol`: the canonical globally unique identifier for the proof
  obligation; it need not be a Python, C++, Go, Swift, or CTest function name;
- `command`: the command or named procedure that owns the proof;
- `proof_kind`: `automated`, `integration`, `hardware`, `human`, or
  `delegated`;
- `proof_status`: `planned` while that clause lacks evidence, or `recorded`
  once the clause's evidence exists. An unchecked step may mix the two states;
  checking a step requires every active mapped clause to be `recorded`.

A recorded clause does not by itself complete or check its owning step. This
separation preserves exact local evidence while other integration, hardware,
human, or deployment obligations remain pending.

Commands prefixed `PLANNED:` are delivery contracts, not claims that the named
test exists today. Some symbols name tests that a future plan step must
implement. Human, hardware, and delegated rows name their review or on-target procedure in the
`command` field rather than pretending those claims are host-automatable.
When implementation lands, replace planned suite-level symbols with the exact
test command and narrow it to the smallest reproducible invocation. The
canonical `test_symbol` may remain stable when an implementation-specific test
function is introduced.

Recorded proofs may not retain a `PLANNED:` command. For every recorded
automated proof, the checker statically tokenizes each compound-command segment,
skips leading environment assignments, and requires its executable to resolve
either as an executable project-relative file or through `PATH`. It does not
run mapped commands.

## Verification

Run the fail-closed map check and its focused negative suite:

```sh
python3 scripts/verification/check_plan_verification_map.py
python3 scripts/verification/verify_plan_verification_map.py
```

The focused suite proves that the checker rejects an unmapped active item, a
duplicate test symbol, an empty command, and stale clause text. Its fixture also
proves that struck-through text is ignored while a completed step remains
active, accepts the legacy no-colon form, rejects malformed verification syntax,
accepts mixed recorded/planned evidence on an unchecked step, rejects a planned
proof on a checked step, rejects `PLANNED:` on a recorded proof, and rejects an
unresolved executable in a compound recorded command. The repository map
additionally requires granular decomposition counts for Lexinet Steps 4 and 6.
Step 4 treats its CLI round trip, trailing focused proof bundle, and focused
race suites as three distinct obligations so none can hide inside a terminal
sentence.

## Oracle gap

A passing map check proves traceability and syntactic completeness. It does not
run the mapped commands, prove that a planned symbol already exists, validate
hardware evidence, or replace human review. The plan step that owns a symbol
must still execute its command and retain the evidence required by that plan.

**Amended 2026-08-07:** created the fail-closed per-plan-step verification map
required by review-amendments Step 13; no hardware or product claim changed.

**Amended 2026-08-17:** made Lexinet Step 4's trailing focused proof bundle and
race invocation explicit mapped obligations; no implementation or completion
claim changed.

**Amended 2026-08-17:** separated per-clause evidence state from whole-step
completion. Unchecked steps may retain exact recorded evidence alongside
pending clauses, while a checked step still fails closed unless every active
mapped clause is recorded.
