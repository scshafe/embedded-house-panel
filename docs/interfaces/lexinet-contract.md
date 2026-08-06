# Provisional Lexinet Contract Boundary

This document records what the device needs; it does **not** claim that Lexinet currently implements these paths or fields. Replace this proposal after inspecting Lexinet itself.

## Required capabilities

1. Request one review card, optionally after a cursor.
2. Submit one idempotent review result.
3. Distinguish “no card available” from request failure.
4. Correlate every request/response without logging credentials or vocabulary content unnecessarily.

## Proposed next-card request

```http
GET /api/v1/vocabulary-review-cards/next?cursor=opaque
Accept: application/json
X-Request-Id: device-generated-uuid
```

```json
{
  "data": {
    "vocabulary_card_id": "opaque-id",
    "word": "perspicacious",
    "definition": "Having a ready insight into things.",
    "example_sentence": "The perspicacious student noticed the pattern.",
    "review_cursor": "opaque-next-cursor"
  },
  "error": null,
  "meta": {
    "request_id": "device-generated-uuid",
    "has_more": true,
    "next_cursor": "opaque-next-cursor"
  }
}
```

## Proposed review mutation

```http
POST /api/v1/vocabulary-review-results
Content-Type: application/json
Idempotency-Key: device-generated-uuid
```

```json
{
  "vocabulary_card_id": "opaque-id",
  "review_rating": "good",
  "reviewed_at": "2026-08-05T19:00:00Z"
}
```

The response should return the complete saved review resource in the same `data/error/meta` envelope.

## Validation boundary

- Enforce HTTPS even though transport is on the tailnet, unless Lexinet's actual deployment has another authenticated channel with equivalent properties.
- Read application credentials from a root-provisioned file descriptor/path, not an environment dump or CLI argument.
- Validate size limits and every required JSON type before producing a `FlashCard`.
- Treat an HTTP success with malformed data as `LEXINET.RESPONSE_SCHEMA_INVALID`, never as an empty card.
- Retry only explicitly transient failures with a bounded policy and idempotency key.
- Do not silently queue review writes while offline in the first implementation. The screen must say that a review was not saved.

## Questions that block the real adapter

- Actual base URL/MagicDNS name and TLS configuration.
- Whether tailnet identity is sufficient or an application token is required.
- Existing paths, envelope, pagination, card IDs, and review rating vocabulary.
- Whether example sentences are stored or generated server-side.
- Whether Lexinet already owns spaced repetition and next-card ordering.
- Expected duplicate/idempotency behavior after a wake or timeout.

