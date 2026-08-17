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

The sync examples make the atomic adjudication boundary visible without adding
a panel-owned wire field:

- `sync-rated-and-voided.json` retains its established exported path while its
  manifest scenario makes the previously invisible boundary explicit:
  sequences 1–4 are immutable replay receipts and sequence 5 is the sole newly
  terminally rejected suffix event;
- `sync-new-suffix-mixed-success.json` proves that one valid new suffix may mix
  accepted and semantic-duplicate receipts; and
- `sync-replay-prefix-then-grouped-terminal-suffix.json` proves that a terminal
  domain failure preserves the replay prefix while every new-suffix receipt
  carries `terminal_rejected`, the first failure code, and no canonical event
  or ingestion ordinal.

On an identical `sync_id` replay, event receipts, the flush receipt, and issued
page bytes are immutable. The live session envelope may advance to current
authoritative lifecycle or state; consumers must not require byte equality for
the whole sync response.
