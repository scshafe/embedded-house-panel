# Spoken Command Catalog

Owns the strict catalog validator, deterministic confusability metrics and
candidate adviser, registered endpoint-effect graph, context projection, fake
backend artifact, and recognition-policy identity. Python standard library
only; no engine or audio dependencies. The sole phrase inventory is
`docs/interfaces/spoken-command-catalog.json`.

**Amended 2026-09-25:** the initial `en-US` ARPABET entries are explicitly
`reviewed_semantic_fixture_not_acoustic` with project fixture provenance. That
label describes the accepted semantic candidates used in deterministic tests;
it is not a claim that a human reviewed their household pronunciation or that
CMUdict was downloaded/pinned. Production pronunciation review, pinned external
dictionary evidence, speaker/room testing, and held-out acoustic qualification
remain pending. All entries are `acoustically_unvalidated`. Reserved agenda
forms have no active projection, and touch-only Undo, recording start and
submission have no spoken forms.

Build and inspect artifacts without writing files:

```sh
python3 -m shared.spoken_command_catalog --root .
```

Add `--output .build/voice` to write deterministic front/back/history bundles
and a pairwise report. Each bundle binds the catalog revision, active-set
digest, fake projection format, recognizer build/model identity, backend
artifact digest, and recognition-policy digest. The policy is recomputed from
canonical bytes on broker load; missing/unknown fields and altered identities
fail closed. `config/voice/recognition-policies/fixture-policy-template.json` supplies explicit
fake-only settings, and the builder produces bound policies for each generated
backend artifact. No backend is described as consuming a universal grammar.

`intent_effect_graph.json` is the explicit endpoint/handler registration used
by the source-independent two-phase graph traversal. The builder cross-checks
all current C++ registry IDs are represented, including disabled touch-only
roots. Phase A emits every reachable endpoint/effect pair and its distinct
effect set, expands enumerated compatible dispatch targets conservatively,
and rejects unknown or unenumerable dispatch. Phase B derives projected IDs
from the catalog and checks every reachable effect, touch-only exclusion,
study effect dominance, and back-only ratings. Pair reviews cannot waive
capture start, submission, power, or illumination. This is an explicit
registration contract, not a claim of automatic C++ whole-program analysis;
new handlers/endpoints must update it and their reducer safeguards together.

The metric version fixes NFC/casefold/punctuation-to-space normalization,
stress removal, unit Levenshtein distance, max-length similarity denominator,
min-length prefix/suffix denominator, both-empty=1 and one-empty=0 behavior,
contiguous containment, explicit invocation stripping, and Cartesian
pronunciation comparisons. Reports retain every variant pair and identify the
risk-maximizing similarity pair. Exact overlapping collisions cannot be
waived; provisional near-neighbor triggers require a recorded fixture review.
The candidate adviser reports global and overlapping-context distances.
These distances are language-design metrics, never safety probabilities.
