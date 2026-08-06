# Panel Mode Protocol

**Status:** version 1 scaffold; supervisor-to-UI delivery is implemented, but a
render-ready acknowledgement is not

**Transport:** local Unix-domain stream socket, newline-delimited UTF-8 JSON

**Authority:** `panel_supervisor` publishes; `vocabulary_panel` consumes.

## Snapshot/event envelope

```json
{
  "data": {
    "protocol_version": 1,
    "event_sequence": 42,
    "display_mode": "vocabulary",
    "presence_state": "present",
    "solar_state": "day",
    "system_state": "active",
    "effective_brightness": 100,
    "observed_at": "2026-08-05T19:00:00Z"
  },
  "error": null,
  "meta": {
    "degraded": false,
    "degraded_reason": null,
    "request_id": "local-42"
  }
}
```

Allowed `display_mode` values are `vocabulary`, `night_red`, `suspended`, and `diagnostic`. Unknown values or protocol versions are hard errors; the UI must blank rather than guess.

## Delivery behavior

- A newly connected UI receives the current snapshot before incremental events.
- `event_sequence` increases monotonically for the supervisor process lifetime.
- At most one UI client is retained. Extra or slow clients are isolated and closed; a reconnect receives the current snapshot rather than replaying stale events.
- With no UI connected, publication retains the snapshot and returns success;
  it is not a disconnection signal or proof of presentation.
- The socket directory is root-owned, setgid to the UI group, and not writable by the UI; the socket itself is group-connectable mode `0660`.
- No command message from the UI changes power state. Future acknowledgements use a distinct message type and do not grant authority.

## Render-readiness limitation

In version 1, a successful notifier call means only that a complete snapshot
was retained and, when a client exists, offered to the local socket transport.
It does **not** establish that the
UI parsed the snapshot, selected the requested view, drew a black/red/day
frame, or completed a DRM/fbdev page flip. The supervisor may notify before it
raises a nonzero backlight value, but that ordering alone cannot guarantee that
an old or bright frame will never be exposed.

Before the backlight/resume path is called product-safe, add a versioned
UI-to-supervisor message with at least the received `event_sequence`, the
rendered `display_mode`, and an explicit presentation result. The supervisor
must reject stale/mismatched acknowledgements, retain brightness zero on
timeout or disconnect, and must not let an acknowledgement grant suspend or
backlight authority. On-target tests must correlate the acknowledgement with a
completed page flip (or the strongest equivalent the selected framebuffer path
provides). Until then, the target services remain disabled by default and the
no-white-flash behavior is an experiment requirement, not a proven invariant.

## Error envelope

```json
{
  "data": null,
  "error": {
    "code": "PANEL_MODE.PROTOCOL_VERSION_UNSUPPORTED",
    "message": "The received protocol version is not supported",
    "details": {
      "received_protocol_version": 2,
      "supported_protocol_versions": [1]
    }
  },
  "meta": {
    "request_id": "local-43"
  }
}
```
