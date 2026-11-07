<!-- @canonical spotify-refresh-token-decision -->

# ADR 0013: Keep a Spotify Refresh Token on the Panel

**Status:** accepted. The owner decided on #69 (2026-11-07) that the panel
sends music to the Bose Portable Home Speaker through Spotify Connect, with
the panel as a remote control only
([music-to-external-speaker.md](../architecture/music-to-external-speaker.md),
option 4). That needs a Spotify refresh token on the panel. This record
writes down where the token lives, how it rotates and how it is revoked
(2026-11-07). Deployment waits for the owner's Spotify app and one-time
sign-in, whose steps come with the sign-in tool.

**Date:** 2026-11-07

## Context

[ADR 0010](0010-use-node-capability-identity-and-pinned-official-tailscale-on-the-panel.md)
keeps the panel free of any Lexinet credential.
[ADR 0012](0012-keep-a-rotating-voice-lab-refresh-token-on-the-panel.md)
made one narrow exception for voice-lab. Spotify has no device-only mode:
every Web API player call acts as a signed-in Premium user. These Spotify
rules were read on 2026-11-07:
- **The grant:** Authorization Code with PKCE, for a public client with no
  client secret. Redirect URIs must be HTTPS or an explicit loopback literal
  (`http://127.0.0.1:PORT`), and `localhost` is refused.
- **Access tokens:** `expires_in` is in seconds; Spotify's examples give
  3600.
- **Refresh:** a PKCE refresh sends `client_id` and the refresh token. The
  response "might" include a new refresh token. When it does not, the client
  keeps using the one it has. Spotify does not say whether the old one stops
  working, so the panel treats every returned token as a rotation.
- **Development mode:** up to 5 allowlisted users, and the app owner must
  have Premium. Extended quota is for organisations only.
- **Revocation:** the user removes the app at
  https://www.spotify.com/account/apps/ ("Remove Access"), or the developer
  deletes the app.
- **Scopes the panel needs:** `user-read-playback-state` (the device list and
  playback state) and `user-modify-playback-state` (transfer, play, pause,
  skip, volume).

## Decision

1. **A second narrow exception to ADR 0010.** The panel keeps one Spotify
   refresh token, for the owner's account and the owner's development-mode
   app. It carries only the two playback scopes. ADR 0010 still holds for
   Lexinet, and ADR 0012 is unchanged.
2. **One holder.** Only `bhp-music-control` holds it. It runs under its own
   service UID, separate from `bhp-panel-ui`, the media broker, the
   voice-lab uploader and the supervisor.
   - **Where it lives:** `spotify-token.json`, `0600`, in a `0700` `token/`
     directory under the service's state directory. All of it is on
     persistent storage outside the image and owned by that UID. No other
     UID can read it. The record names the client ID it belongs to, and a
     token saved for another client is never used.
   - **Where it never goes:** Git, a pull request, an image or overlay, a
     command argument, an environment variable, a URL, a log, a diagnostic
     bundle, an evidence record, a crash report or an agent transcript.
   - **The access token** lives only in the service's memory and is never
     written down.
   - **The UI** never sees either token. The music protocol carries only
     the speaker's name and the playback state.
3. **Sign-in happens once, on the owner's PC, by the owner.**
   - `bhp-music-spotify-sign-in` runs under the owner's own account, never
     the agent's.
   - It listens on `127.0.0.1` only, and checks the `state` it sent.
   - It keeps the PKCE verifier in memory.
   - It writes the refresh token straight into a new `spotify-token.json`
     (`0600`, in a `0700` directory on a tmpfs).
   - It prints only where it saved it.
   - The owner then copies that file to the panel over ssh and deletes the
     PC copy.
   - There is no client secret anywhere: PKCE does not use one, and the
     Spotify dashboard's secret is never copied.
4. **Refresh is single-flight, and a new token is durable before use.**
   - One refresh at a time, process-wide.
   - When a refresh returns a new refresh token, it replaces the old one on
     disk before the new access token is used: temporary file, fsync,
     rename, directory fsync (`shared/durable_store`'s `DurableFile`).
   - If that write fails, the service discards the new access token and
     shows "sign in again", because the token on disk may already be dead.
   - An `invalid_grant` also ends in "sign in again", and the record is
     replaced by a signed-out one.
   - A 401 from the Web API forgets the access token and refreshes once. A
     second 401 means sign in again.
5. **Revocation is the owner's:**
   - "Remove Access" for the app at https://www.spotify.com/account/apps/,
     which ends the refresh token;
   - deleting the app in the Spotify developer dashboard, which ends every
     token it issued;
   - deleting `spotify-token.json` on the panel, which signs the panel out.

   An issued access token keeps working until it expires, about an hour.

## Threat model

| Threat | Control | Accepted limitation |
|---|---|---|
| Physical or root access copies the token file | `0600`, its own UID, never in images or diagnostics | Until revoked, a thief can control the owner's Spotify playback and read what is playing on any of the owner's devices. That is all the two scopes allow: no library, no playlists and no account details. Spotify gives refresh tokens no stated expiry. No encrypted or hardware-bound storage is assumed. Loss of the panel means removing the app's access too |
| A copied token is used alongside the panel | Every rotation the panel sees is saved before use; `invalid_grant` shows "sign in again" | Spotify does not document whether a rotated token stays valid, so a copy may keep working until access is removed |
| The token passes through a PR, a log or a transcript | The sign-in tool writes it straight to a file and prints only the path. The owner runs it in the owner's account and copies the file with their own ssh key. Logs carry codes, never values | A root-level process dump can read memory |
| A compromised UI | The UI holds no token. It can send only the closed music requests for the one configured speaker | A compromised UI can play, pause, skip and change the volume on that speaker |
| Someone at the panel | The surface plays, pauses, skips and sets volume on the configured speaker only. It cannot sign in, sign out or pick another device | Anyone in the room can control the music. The same is true of the speaker's own buttons |
| A crash between rotation and the durable write | New token written before use; a failed write means signing in again | One extra sign-in in that rare case |

## Alternatives

- **A refresh token per session, signing in each time.** The panel has no
  browser, and a sign-in needs a PC. Rejected.
- **Spotify's local ZeroConf `addUser`.** It is "available only for approved
  partners". Not available.
- **Keeping the token in the UI process.** That puts a secret in the process
  with the largest surface. Rejected, as in ADR 0012.
- **Signing in on the panel through an ssh port forward.** It would keep the
  token off the PC's disk, but it needs an aarch64 build of the sign-in tool
  and the owner's ssh session to the board. It is kept as a later option.
  The tmpfs copy on the PC is deleted right after the transfer.

## References

- #69, the owner's decision (2026-11-07)
- [Music to an external speaker](../architecture/music-to-external-speaker.md)
- [Security and trust map](../security/threat-model.md)
- [ADR 0010](0010-use-node-capability-identity-and-pinned-official-tailscale-on-the-panel.md),
  [ADR 0012](0012-keep-a-rotating-voice-lab-refresh-token-on-the-panel.md)
- Spotify: [PKCE flow](https://developer.spotify.com/documentation/web-api/tutorials/code-pkce-flow),
  [refreshing tokens](https://developer.spotify.com/documentation/web-api/tutorials/refreshing-tokens),
  [redirect URIs](https://developer.spotify.com/documentation/web-api/concepts/redirect_uri),
  [quota modes](https://developer.spotify.com/documentation/web-api/concepts/quota-modes),
  [removing an app's access](https://support.spotify.com/us/article/spotify-on-other-apps/)
