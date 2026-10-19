# Shared durable store and outbox mechanics

Added 2026-10-19 for issue #23
([recorder groundwork plan](../../.claude/plans/2026-10-19-voice-lab-recorder-groundwork.md),
Step 3). Three mechanisms the panel's outboxes share. What to store, what to
send, credentials and retry policy stay with each domain.

- **`DurableFile`** (`@canonical durable_file`) is one owner-only, bounded,
  atomically replaced file. It was taken out of `vocabulary_panel`'s
  `FileStudyStore`, which now wraps it with the study's file names and
  `STUDY.STORAGE_*` codes.
  - The directory is 0700 and owned by the process user.
  - An exclusive `flock` allows one writer per directory.
  - A commit writes a temporary file, fsyncs it, renames it and fsyncs the
    directory.
  - A crashed temporary file is never read.
  - A failed directory fsync stays failed until restart.
- **`Backoff`** (`@canonical bounded_backoff`) doubles the delay from
  `initial` up to `maximum`, with downward jitter from injected randomness.
- **`ResumableUpload`** (`@canonical resumable_upload`) sends one file in
  chunks, one network action per `step()`.
  - It starts from the server's own offset and asks again after any failed
    send.
  - A refusal stops it for good.
  - An offset beyond the file or moving backwards is
    `UPLOAD.SERVER_OFFSET_INVALID`.
  - The transport is two ports the domain supplies (`server_offset`,
    `put_range`), so the voice-lab wire format stays out of this library.

Tests: `durable_store_test`, `backoff_test`, `resumable_upload_test`; the
study store's crash test still runs through `FileStudyStore`.
