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
  - The panel's repeat counts add to the fingerprint's record.
  - The agent writes `status/` (decision 8's statuses, a PR number, and a
    private one-line reason the panel never sees).
  - Retention (decision 9) deletes a report 90 days after it closes, and its
    audio as soon as it is transcribed and closed.
- **`redact.py`** builds the public summary of section 6 from an allowlist
  of closed fields. Each field is checked again, and a field that fails stops
  the summary instead of passing through. Descriptions, transcripts, log
  lines, locations, presence, trace IDs and addresses are never read into it.
- **`server.py`** is the receiver, implementing the v1 API of
  `bug_reporter/inbox_client.h`.
  - It serves TLS from `tailscale cert` on the PC's tailnet address.
  - It admits a peer only if `tailscale whois` shows the panel's tag.
  - It logs codes and report IDs, never content.
  - It runs the owner's speech-to-text command (whisper.cpp, decision 4) on
    each spoken description.
- **`python3 -m bhp_report_inbox`** gives `serve` and the agent's commands:
  - `list [--new]`;
  - `show ID`: paths and closed fields, never the description or logs;
  - `set-status ID STATUS [--pr N] [--reason TEXT]`;
  - `public-summary ID [--markdown]`;
  - `retention [--days 90]`.

## Tests

- `python3 -m unittest discover -s report_inbox/tests` covers:
  - every manifest rule;
  - uploads, resume, a corrupt file, idempotent completion, counts, statuses
    and retention;
  - the redactor against planted private text and injected fields;
  - the receiver over loopback (the panel's calls, a refused peer,
    transcription with a fake whisper);
  - the `tailscale whois` check;
  - the CLI.
- The CTest `bug_reporter_inbox_contract_test` runs the panel's real
  `bhp-reporter` and `bhp-log-tee` against this receiver, pinning the API
  from both sides.
