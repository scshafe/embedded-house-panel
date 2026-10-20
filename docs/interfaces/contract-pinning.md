# Pinning Another Project's Contract

<!-- @canonical contract-pinning -->

**Status (2026-10-19):** the mechanism behind two pins: lexinet's `study.v1`
and voice-lab's `corpus-client.v1` ([the voice-lab contract](voice-lab-contract.md),
issue #23).

## What a pin is

The panel never keeps its own copy of another project's schema. It keeps three
things:

1. **A provenance file** checked in next to the exported fixtures.
   - `source.repository_base_revision`: the full commit SHA of the contract
     repository that the fixtures came from.
   - The SHA-256 of the manifest, of the OpenAPI document or schemas, and of
     the whole contract bundle.
   - One entry per exported example: source path, exported path, schema,
     expected outcome and SHA-256.

   The revision says where to look. The digests say what must be there. A
   matching revision with different content fails.
2. **The exported fixtures**, byte for byte, which the panel's own tests
   decode.
3. **An exporter with `--check`.** It re-exports from the pinned source and
   compares every byte with what is checked in. It writes nothing in check
   mode.

## Getting the pinned source

`scripts/contracts/contract_pin.py` (`@canonical contract_pinned_source`)
resolves the source for any contract. A `ContractPin` names four things:
- the contract (upper case, for example `LEXINET`; this is also the
  error-code prefix);
- the public repository URL;
- the provenance file;
- an ignored cache directory under `.build/`.

When no explicit checkout is given, the tool reads the revision from the
provenance file. It shallow-fetches that one commit into
`<cache>/<sha>`, then checks that HEAD is the pin, that the checkout is its
own top level, and that no tracked file changed. A cache that fails the
check is refused with `<NAME>_CACHE_INVALID`; delete it and fetch again. The
tool never infers a sibling or workstation checkout. An explicit checkout,
for repinning, is used as given.

`scripts/lexinet/pinned_source.py` is the lexinet configuration:
- pin `LEXINET`;
- `https://github.com/scshafe/lexinet`;
- the study.v1 provenance under `vocabulary_panel/tests/fixtures/study_v1/`;
- the cache `.build/lexinet/source/`.

## Adding a contract

voice-lab's pin was added this way:

1. A `ContractPin` configuration beside the contract's tools:
   `scripts/voice_lab/voice_lab_pin.py` is `ContractPin("VOICE_LAB",
   "https://github.com/scshafe/voice-lab", <provenance>,
   <ROOT>/.build/voice-lab/source)`, with the provenance under
   `voice_lab_recorder/tests/fixtures/corpus_client_v1/`.
2. An exporter for the bundle's own manifest layout. Each contract defines
   its own layout, so exporters are per contract; the pin and the fetch are
   shared. It has `--check`, and an explicit-checkout option for repinning.
3. The provenance and the exported fixtures checked in, with the `--check`
   run in the plan's verification map.
4. A live check against the contract owner's server from the same pinned
   source (`make verify-lexinet-live`; voice-lab's is its wire plan's Step 2).
5. An interface document listing the pin and the repin commands, as
   [the lexinet contract](lexinet-contract.md) and
   [the voice-lab contract](voice-lab-contract.md) do.

Tests: `scripts/contracts/tests/test_contract_pin.py` runs the mechanism under
the name `VOICE_LAB` against a local repository, and
`scripts/lexinet/tests/test_pinned_source.py` runs the lexinet configuration,
and `scripts/voice_lab/tests/` runs the voice-lab exporter against a synthetic
bundle.
