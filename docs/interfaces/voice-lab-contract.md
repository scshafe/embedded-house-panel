# voice-lab Corpus Client Contract

<!-- @canonical voice-lab-contract -->

**Status (2026-10-19):** voice-lab's `corpus-client.v1` bundle is pinned here
for the recorder (issue #23). The bundle is voice-lab's D-056 step 5
(voice-lab#108, #109). The panel's take package, intake and decisions are
the [recorder wire plan](../../.claude/plans/2026-10-19-voice-lab-recorder-wire.md);
none of them runs against the PC until the board is on the tailnet.

The wire authority is voice-lab's `contracts/corpus-client.v1/` at the pinned
revision:
- `openapi.json`, the routes a corpus client's token reaches;
- `schemas/`, the JSON Schemas of every record;
- `PROTOCOL.md`, how to use them;
- `manifest.json`, which names the required semantics and the examples and
  binds every other file by SHA-256 and size.

The panel keeps no second schema.

## The pin

The checked-in
[`contract-provenance.json`](../../voice_lab_recorder/tests/fixtures/corpus_client_v1/contract-provenance.json)
pins:

- contract `corpus-client.v1`, schema version `1`, manifest version `1`;
- voice-lab revision `8f516567a38a1ef10cae23060623633791b24b2f`;
- `manifest.json` SHA-256
  `845038524025c37d09e30ec6efdc8dcf6334e55dbecf5e16f8d534e7bdf6475b`;
- the bundle digest
  `496302a49ba4123816a87dd487d0fb175c02e3bf9448cf21ddf1a60f3db945d1`.

The bundle digest is voice-lab's rule: every file of the bundle, its
`manifest.json` included, in sorted path order, hashed as
`path NUL contents NUL` into one SHA-256. The provenance file also lists the
18 exported examples, each with its schema, expected outcome (an invalid one
with where and why the PC refuses it), SHA-256 and size.

Check the export against the pinned commit. The tool fetches it from
`https://github.com/scshafe/voice-lab` into `.build/voice-lab/source/`:

```sh
python3 scripts/voice_lab/export_corpus_client_v1_fixtures.py --check
```

The check fails unless:
- every bound file matches its binding;
- no file in the bundle is unbound;
- the contract identity is the pinned one;
- every exported example is byte-identical.

To repin, export from an explicit checkout at the new commit, then review the
diff of the examples:

```sh
python3 scripts/voice_lab/export_corpus_client_v1_fixtures.py \
  --voice-lab-source-dir "$BHP_VOICE_LAB_SOURCE_DIR"
```

The fetch and the cache check are the shared mechanism in
`scripts/contracts/contract_pin.py`; see
[pinning another project's contract](contract-pinning.md).

## What the panel checks offline

`voice_lab_corpus_client_v1_contract_fixture_test` (part of `make verify`)
reads only the checked-in copy:
- the pin;
- every file against its provenance;
- the golden take's `original.wav`, rebuilt from its recipe to the recipe's
  SHA-256 and size;
- the take id, the `READY` digests and the continuity digest, binding the five
  files to each other;
- the `alsa-continuity/v1` rule in integer nanoseconds (62,500 ns a frame
  at 16 kHz): the golden take's maximum deviation, and the lost period that
  the invalid example shows;
- the canonical form: each package record and each vector, byte for byte.

## Compatibility

As voice-lab's bundle README states it:
- the PC may add optional fields to its answers within `corpus-client.v1`,
  and the panel ignores fields it does not need;
- requests and package records stay strict, so the panel writes no field the
  schema does not name;
- a new required field or semantic, or a changed meaning, is a new contract
  and a coordinated update of the panel.

## The live check

voice-lab's fixture server, `python -m cole_voice_lab.contract_fixture
serve`, runs the PC's real API with a fake Pocket ID that behaves as v2.13
does. `make verify-voice-lab-live` will run it from the pinned checkout; it
is the wire plan's Step 2.
