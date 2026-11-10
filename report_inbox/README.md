# Report Inbox

<!-- @canonical report_inbox -->

The PC side of panel bug reports: step 4 of the accepted
[bug-report design](../docs/architecture/bug-reports-and-self-repair.md). It
runs on the owner's PC, never on the panel, and uses Python 3's standard
library only, so the PC needs nothing built. The owner's oversight session
installs it ([bug-report-inbox-setup.md](../docs/operations/bug-report-inbox-setup.md)).

## Pieces

- **`validate.py`** checks every manifest against `bhp.bug-report/v1` again,
  with the panel's own rules (`bug_reporter/report_bundle.h`): closed
  tokens, dotted codes, bounded text, the file names and sizes, and hashes.
- **`store.py`** keeps the inbox's files: `incoming/`, `reports/`,
  `status/`, `fingerprints/` and `transcripts/`.
  - Uploads are contiguous chunks, and a retried chunk changes nothing.
  - `complete` checks every file's size and SHA-256, then moves the report
    into `reports/` in one rename. A corrupt file is discarded so the panel
    sends it again.
  - The panel's repeat counts add to the fingerprint's record, once per
    batch ID: the record keeps its 64 most recent IDs, so a retried update
    after a lost answer adds nothing.
  - The agent writes `status/` (decision 8's statuses, a PR number, and a
    private one-line reason the panel never sees). The receiver reads a
    status file only if it is a regular file of at most 4 KiB, never through
    a link and never blocking on a FIFO.
  - Retention (decision 9) deletes a report 90 days after it closes, and its
    audio as soon as it is transcribed and closed. It also clears uploads
    abandoned for 7 days and fingerprints with no report left. A status whose time
    is malformed counts as open, so it never stops retention for the others.
- **`redact.py`** builds the public summary of section 6 from an allowlist
  of closed fields. Each field is checked again, and a field that fails stops
  the summary instead of passing through. Descriptions, transcripts, log
  lines, locations, presence, trace IDs and addresses are never read into it.
- **`server.py`** is the receiver, implementing the v1 API of
  `bug_reporter/inbox_client.h`.
  - It serves TLS from `tailscale cert` on the PC's tailnet address, never a
    wildcard.
  - It admits a peer only if `tailscale whois` shows the panel's tag, before
    the TLS handshake. A malformed whois answer refuses.
  - **The accept loop never waits on a peer.**
    - It closes a peer that the cached whois answer already refuses.
    - The peer check and the handshake run in the connection's thread, with
      30 s per step and 120 s for the whole connection.
    - At most 8 connections run at once, and the last slot is kept for
      known panel peers.
    - Refusal lines are rate-limited.
    - Concurrent whois lookups for one address run once.
  - It logs codes and report IDs, never content.
  - It runs the owner's speech-to-text command (whisper.cpp, decision 4) on
    each spoken description, after checking it is 16 kHz mono 16-bit PCM.
    - On start it queues again any open report's audio without a
      transcript.
    - Audio it gives up on gets `transcripts/<id>.refused`: the wrong
      format, or three failed runs. Such audio is not tried again.
    - A failure in any one report never stops the transcription thread.
- **`python3 -m bhp_report_inbox`** gives `serve` and the agent's commands:
  - `list [--new]`;
  - `show ID`: paths and closed fields, never the description or logs;
  - `set-status ID STATUS [--pr N] [--reason TEXT]`;
  - `public-summary ID [--markdown]`;
  - `retention [--days 90] [--upload-days 7]`.

## Tests

- `python3 -m unittest discover -s report_inbox/tests` covers:
  - every manifest rule;
  - uploads, resume, a corrupt file, idempotent completion, counts, statuses
    and retention;
  - the redactor against planted private text and injected fields;
  - planted status files: a link, a FIFO, a directory, an oversized file;
  - the receiver over loopback (the panel's calls, a refused peer,
    transcription with a fake whisper, audio in the wrong format, audio
    queued again on start);
  - the receiver over TLS: an idle connection that neither stalls other
    peers nor stays past its timeout, and the connection cap;
  - the `tailscale whois` check, including malformed answers, and the listen
    address;
  - the CLI.
- The CTest `bug_reporter_inbox_contract_test` runs the panel's real
  `bhp-reporter` and `bhp-log-tee` against this receiver, pinning the API
  from both sides.
