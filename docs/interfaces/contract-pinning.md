# Pinning Another Project's Contract

<!-- @canonical contract-pinning -->

**Status (2026-10-19):** the mechanism behind the lexinet `study.v1` pin,
written down for the second contract, voice-lab's `contracts/corpus-client.v1/`
bundle (issue #23). The voice-lab pin does not exist yet: voice-lab publishes
the bundle in its D-056 step 5.

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

## Adding a second contract

For voice-lab, once the bundle is published:

1. Add a `ContractPin("VOICE_LAB", "https://github.com/scshafe/voice-lab",
   <provenance>, <ROOT>/.build/voice-lab/source)` beside the recorder's
   fixtures.
2. Write the exporter for the bundle's own manifest layout. Each contract
   defines its own layout, so exporters are per contract; the pin and the
   fetch are shared. Give it `--check`, and an explicit-checkout option for
   repinning.
3. Check in the provenance and the exported fixtures, and add the `--check`
   run to the plan's verification map, as lexinet's is.
4. Add a live check, like `make verify-lexinet-live`, against voice-lab's
   fixture server from the same pinned source.
5. Write `docs/interfaces/voice-lab-contract.md`, listing the pin and the
   repin commands, as [the lexinet contract](lexinet-contract.md) does.

Tests: `scripts/contracts/tests/test_contract_pin.py` runs the mechanism under
the name `VOICE_LAB` against a local repository, and
`scripts/lexinet/tests/test_pinned_source.py` runs the lexinet configuration.
