<!-- @canonical voice-lab-refresh-token-decision -->

# ADR 0012: Keep a Rotating voice-lab Refresh Token on the Panel

**Status:** accepted. The owner decided in issue #23 (2026-10-18) that the
panel signs in to voice-lab with Pocket ID's device-code grant and keeps the
refresh token in a `0600` file owned by the uploader's user. This record writes
down the rules and the threat model (2026-10-19). Deployment waits for
voice-lab's `contracts/corpus-client.v1/` bundle, for the panel's tailnet
enrollment, and for the uploader that holds the token.

**Date:** 2026-10-19

## Context

[ADR 0010](0010-use-node-capability-identity-and-pinned-official-tailscale-on-the-panel.md)
keeps the panel free of any Lexinet credential: Lexinet authorizes the tagged
tailnet node itself. voice-lab authorizes differently. A take enters the
owner's training corpus under his subject, so voice-lab asks the person, not
the device, through its Pocket ID instance (D-056, scshafe/voice-lab#86). The
public client `voice-lab-panel` uses the OAuth 2.0 device authorization grant
(RFC 8628). Access tokens last 15 minutes. Refresh tokens last 30 days,
sliding.

voice-lab checked these Pocket ID v2.13.0 behaviours against that tag's source
and posted them on #23:
- **Rotation:** the refresh token rotates on every refresh, with no grace
  period. Reusing a rotated token revokes the whole token family
  (`invalid_grant`).
- **The complete link:** opening `verification_uri_complete` while signed in
  approves the device on page load once consent is recorded. v2.15 removed
  this.
- **Polling:** it answers `authorization_pending` at a 5 s interval and never
  `slow_down`. An expired, unapproved code keeps answering pending.
- **Audience:** a `resource` parameter changes `aud` so that it no longer
  names `voice-lab-panel`.
- **Reach:** the token reaches only voice-lab's allow-list:
  - meta;
  - the catalog;
  - the panel profile's library and take history;
  - intake;
  - decisions on panel takes;
  - request status.

The tailnet grants `tag:house-panel` → `tag:voice-lab` on 443 (infra #19)
and → the Pocket ID host on 443 (infra #21) are applied. The panel is not
enrolled yet.

## Decision

1. **One narrow exception to ADR 0010.** The panel keeps exactly one
   long-lived secret: the Pocket ID refresh token for the client
   `voice-lab-panel`, acting for the owner's subject. ADR 0010 still holds
   for Lexinet: no study credential on the panel.
2. **One holder.** Only the voice-lab uploader holds the token: a small
   process under its own service UID, separate from `bhp-panel-ui`, the media
   broker and the supervisor.
   - **Where it lives:** a `0600` file in a `0700` directory on persistent
     storage, both owned by that UID, outside the image. No other UID can
     read it.
   - **Where it never goes:** Git, an image or overlay, a command argument,
     an environment variable, a URL, a log, a diagnostic bundle, an evidence
     record, or a crash report.
   - **The access token** lives only in the uploader's memory and is never
     written down.
3. **Sign-in is the device-code grant, by the owner.** The uploader:
   - requests a device code with `client_id` only and no `resource`
     parameter;
   - hands the panel `verification_uri` and the user code to display, never
     `verification_uri_complete`;
   - polls at the server's interval;
   - stops at `expires_in` by its own clock, because Pocket ID will not;
   - treats every token as opaque.

   The owner approves on his phone.
4. **Refresh is single-flight, and the new token is durable before use.**
   - One refresh at a time, process-wide.
   - The new refresh token replaces the old one on disk before the new
     access token is used: temporary file, fsync, rename, directory fsync
     (`shared/durable_store`'s `DurableFile`).
   - If that write fails, the uploader discards the new access token and
     enters "sign in again", because the token on disk was already rotated
     away.
   - An `invalid_grant` also ends in "sign in again", and the old file is
     removed.
   - Nothing retries a refresh with a token already sent.
5. **Revocation is the owner's, in three places:**
   - removing `voice-lab-panel` from his authorized apps in Pocket ID (or
     deleting the client), which ends the refresh token;
   - removing the panel's node or its grants in the tailnet policy, which
     ends the path;
   - reuse of a rotated token, which Pocket ID treats as theft and revokes
     the family.

   An issued access token still works for at most 15 minutes.

## Threat model

| Threat | Control | Accepted limitation |
|---|---|---|
| Physical or root access copies the token file | `0600`, its own UID, never in images or diagnostics | A thief gets at most voice-lab's allow-list under the owner's subject for up to 30 days, sliding, unless revoked. No encrypted or hardware-bound storage is assumed. Loss of the panel means revoking `voice-lab-panel` as well as the node, the Lexinet grant and the companion key |
| A copied token is used alongside the panel | Rotation without grace: whichever side refreshes second presents a rotated token and Pocket ID revokes the family | The first user of a stolen token wins until the next refresh; the panel then fails with `invalid_grant` and the owner sees "sign in again", which is the signal to revoke and re-enroll |
| A compromised UI or broker | Neither can read the file; the uploader takes only finished takes and the owner's decisions | A compromised UI can still misrepresent what it asks the uploader to send within the allow-list |
| Someone near the panel opens the sign-in link | Only `verification_uri` and the code are shown; approving needs the owner's Pocket ID session | Shoulder-surfing the code during the 15-minute window still needs the owner's approval |
| A token leaks through logs or arguments | Structured logs carry codes, never values; the token never reaches an argument, URL or environment | A root-level process dump can read memory |
| Crash between rotation and the durable write | New token written before use; a failed write means signing in again | One extra sign-in in that rare case |

## Alternatives

- **A voice-lab node-capability mode like Lexinet's.** voice-lab's D-056
  authorizes the person for corpus writes, so it offers no device-only mode.
  Not available.
- **Signing in on every upload.** Without a stored refresh token, the owner
  would approve on his phone at least every 15 minutes of uploading. Rejected
  in #23.
- **Keeping the token in the UI process.** That puts a secret in the process
  with the largest surface, and a 600 s upload would share a process with the
  shell. Rejected by #22 and #23.

## References

- Issue #23 and the voice-lab D-056 summary posted there (2026-10-19)
- [Recorder groundwork plan](../../.claude/plans/2026-10-19-voice-lab-recorder-groundwork.md), Steps 5 and 7
- [Security and trust map](../security/threat-model.md)
- [ADR 0010](0010-use-node-capability-identity-and-pinned-official-tailscale-on-the-panel.md)
