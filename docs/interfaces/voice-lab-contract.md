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

## What the panel writes

The recorder writes the five-file take package with
`voice_lab_recorder::write_take_package` (`take_package.h`). Its canonical
JSON writer formats floats as Python's `repr` from `std::to_chars`:
nlohmann's `dump()` switches to exponent form at a different point and does
not always give the shortest digits.
`voice_lab_canonical_json_take_package_test` passes every vector and rebuilds
the golden take's records byte for byte.

`voice_lab_recorder::CorpusClient` (`corpus_client.h`) sends the intake, the
decisions and the library reads, and classifies answers as PROTOCOL.md
section 7 does: retryable, sign in again, or refused (not retried).

## Compatibility

As voice-lab's bundle README states it:
- the PC may add optional fields to its answers within `corpus-client.v1`,
  and the panel ignores fields it does not need;
- requests and package records stay strict, so the panel writes no field the
  schema does not name;
- a new required field or semantic, or a changed meaning, is a new contract
  and a coordinated update of the panel.

## The live check

`make verify-voice-lab-live` runs voice-lab's fixture server,
`python -m cole_voice_lab.contract_fixture serve`, from the pinned checkout
(or `BHP_VOICE_LAB_SOURCE_DIR`). The server is the PC's real API with a fake
Pocket ID that behaves as v2.13 does. `scripts/voice_lab/live_contract.py`:

- makes a venv under `.build/voice-lab/live-contract/` with CPython 3.12
  (`--python`, `BHP_VOICE_LAB_PYTHON`, `python3.12` or uv's) and the API
  image's hash-locked closure, `requirements/vault-linux.txt`; the package
  runs from the checkout through a `.pth` file and is never built;
- serves on loopback with a throwaway self-signed certificate, which the
  client trusts as a test-only anchor;
- puts the fixture's root in the temporary directory. On btrfs,
  `os.listdir()` on a directory descriptor opened earlier misses entries
  created since, and voice-lab's corpus store then refuses its own database
  (`CORPUS.STORE_UNSAFE`); tmpfs lists them;
- runs `voice_lab_recorder_live_contract_client` once per scenario, each a
  new process sharing the token store, and writes `report.json`. The key,
  the token store and the fixture's root are deleted after the run, and no
  token is printed.

| Scenario | Checks |
| --- | --- |
| `sign_in` | discovery; the device code with `verification_uri` and the user code; one `authorization_pending`, then approval; the refresh token saved |
| `library` | restored from disk; a refresh that rotates the saved token before use; `GET /api/v1/catalog` with an `ETag`, then `304`; the panel's library view (`?profile=xvf3800-linear-4`) with its `ETag`, then `304`; the desktop view refused `403 client_scope`; no token refused `401` |
| `intake_interrupted` | the golden take packaged by `write_take_package` and declared; the link drops after two 16 KiB chunks of `original.wav`, and the session shows 32,768 bytes held |
| `intake_resume` | a new process declares again (the session kept its bytes), resumes from the server's offset, finalizes, and the session is `accepted` with its receipt; the library lists the take |
| `decide` | `kept` with a new `req-` id and the history's revision is applied; the same request again answers the same outcome; the revision advanced once; a stale revision is refused `revision_conflict`; the prompt is `kept` in the panel's view |
| `reuse_revokes` | a refresh, then the rotated-away token sent again: `invalid_grant`; the session's next refresh fails because the family is revoked, signs out and clears the store |

Without `--execute` the script prints its plan and touches nothing. It is
never part of `make verify`: it needs the network, CPython 3.12 and openssl.
