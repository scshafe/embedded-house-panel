# Structured Logging

Owns the one canonical JSON log encoder used by all user-space processes.

Public interface: `bhp::StructuredLogger`. It writes one UTF-8 JSON object per line with the mandatory `timestamp`, `level`, `message`, `service`, and `trace_id` fields. Error calls additionally carry a machine-readable code, message, context, and stack trace string.

It depends only on the C++17 standard library. Every call checks both stream
insertion and flush state and throws
`STRUCTURED_LOGGING.OUTPUT_STREAM_WRITE_FAILED` when the record was not fully
delivered, allowing callers to turn logging failure into a typed domain fault.
Callers must never place secrets, credentials, authorization headers, or private
payloads in `context_fields`; the logger cannot infer which arbitrary values are
sensitive.
