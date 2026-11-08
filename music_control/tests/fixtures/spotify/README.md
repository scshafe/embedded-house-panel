# Spotify fixtures

Answers the panel's Spotify client is tested against. None was recorded from a
live account: the owner's app and sign-in do not exist yet (ADR 0013), and CI
makes no live calls. Each file follows the shape of the example in Spotify's
Web API reference, read 2026-11-07:

- `token-*.json`: the token endpoint (Authorization Code with PKCE, and
  refreshing tokens). `token-refresh-kept.json` is a refresh answer without a
  new refresh token, which Spotify says can happen.
- `token-invalid-grant.json`: the authentication error object (RFC 6749
  `error` and `error_description`).
- `devices.json`: Get Available Devices. One device has a `null` ID, which the
  reference allows; the client leaves it out.
- `playback-speaker.json`: Get Playback State, a track on the speaker.
- `playback-phone.json`: the same, playing on another device.
- `error-*.json`: the regular error object (`status`, `message`, `reason`).
  `PREMIUM_REQUIRED` and `NO_ACTIVE_DEVICE` are player reasons Spotify sends
  but the reference does not list.

Names and IDs are invented. Replace or extend them with recorded answers once
the owner's sign-in exists, with tokens removed.
