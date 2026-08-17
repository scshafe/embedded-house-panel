# Lexinet `study.v1` Exported Fixtures

These JSON examples are byte-for-byte exports from the Lexinet-owned contract,
not a panel-owned schema. `contract-provenance.json` pins the Lexinet repository
base revision, canonical contract paths, exact protocol/schema identity,
required semantics, source digests, and each exported fixture digest.
The bundle digest hashes each sorted contract-relative path, a NUL byte, its raw
contents, and a trailing NUL byte for the manifest, OpenAPI document, and every
manifest-listed example.

Refresh or verify the export only from an explicit checkout:

```sh
python3 vocabulary_panel/tools/export_study_v1_contract_fixtures.py \
  --lexinet-source-dir "$BHP_LEXINET_SOURCE_DIR"
python3 vocabulary_panel/tools/export_study_v1_contract_fixtures.py \
  --lexinet-source-dir "$BHP_LEXINET_SOURCE_DIR" --check
```

The host test consumes only this bounded export. It checks exact `study.v1`
identity, verifies every example digest, accepts optional response additions,
and rejects an uncoordinated required semantic as
`LEXINET.RESPONSE_SCHEMA_INVALID`.
