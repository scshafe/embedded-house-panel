# Owner Setup: the Spotify App and the One-Time Sign-In

<!-- @canonical spotify-owner-setup -->

**Status (2026-11-07):** ready for the owner. Nothing here has been done
yet. These are the owner's steps for
[ADR 0013](../decisions/0013-keep-a-spotify-refresh-token-on-the-panel.md):
- create the Spotify developer app;
- sign the panel in once;
- put the refresh token on the panel.

They are written so that no client secret or token ever passes through a pull
request, a log or an agent's transcript.

## The rules for these steps

- **The client secret is never used.** The panel signs in with PKCE, which
  has no client secret. The dashboard shows one anyway. Do not reveal it,
  copy it or paste it anywhere.
- **The refresh token goes straight from Spotify into a file.** Only these
  steps' commands write or move that file. Never print it: no `cat`, `less`,
  editor, screenshot or paste. Never send it to the agent.
- **Run everything in your own account on the PC** (`cole`), from your own
  checkout. The agent's account cannot read your files, and these steps never
  run in the agent's session.
- **What you may tell the agent** (none of it is secret): "done", the app's
  client ID, and the speaker's Spotify device name.

## Part 1: create the Spotify app (once, about 5 minutes)

1. Open https://developer.spotify.com/dashboard and log in with the Premium
   account the music plays from. Accept the developer terms if asked.
2. Choose **Create app** and fill in:
   - **App name:** `House panel`
   - **App description:** `Controls Spotify Connect playback on my speaker
     from my house panel.`
   - **Website:** leave empty.
   - **Redirect URIs:** `http://127.0.0.1:8888/callback`, exactly, then
     **Add**. Spotify refuses `localhost`; it must be `127.0.0.1`.
   - **Which API/SDKs are you planning to use:** tick **Web API** only.
   - Accept the terms and choose **Save**.
3. In the app's **Settings**, note the **Client ID**. It is not a secret.
   Do not choose **View client secret**.
4. Under **User Management**, add your own Spotify account (your name and the
   account's email). The app stays in **Development mode**. That allows up to
   5 listed users, and it works only while the app's owner has Premium.
5. Find the speaker's Spotify device name. Play something in the Spotify app
   on your phone and open the device picker (Spotify Connect). Note the Bose
   speaker's name exactly as shown, with the same capitals and spaces. That
   is the name the panel looks for.

The dashboard's screens may be labelled a little differently. What matters is
the redirect URI, Web API, and your account in the user list.

## Part 2: sign the panel in (once, when the panel step is ready)

The sign-in tool is `bhp-music-spotify-sign-in`
([music_control/README.md](../../music_control/README.md)).
- It listens on `127.0.0.1:8888` only and waits up to 5 minutes for one
  redirect.
- It writes `spotify-token.json` (`0600`, in a `0700` directory).
- It prints only where it saved the file.

The token goes to `/run/user/<your UID>`, a tmpfs that only you can read and
that is gone after logout or a reboot. Do the sign-in and Part 3 in one
sitting.

1. Build the tool in your checkout:

   ```sh
   cd ~/src/embedded-house-panel
   git pull
   cmake --preset host-dev
   cmake --build .build/host-dev --target bhp-music-spotify-sign-in
   ```

2. Run it, with your client ID in place of `CLIENT_ID`:

   ```sh
   .build/host-dev/music_control/bhp-music-spotify-sign-in \
     --client-id CLIENT_ID \
     --token-dir "/run/user/$(id -u)/bhp-music/token"
   ```

3. It prints an `https://accounts.spotify.com/authorize?...` address. Open it
   in a browser **on this PC**: the redirect comes back to `127.0.0.1`, so
   another device cannot complete it.
   - The address holds your client ID, the two playback scopes
     (`user-read-playback-state`, `user-modify-playback-state`), the redirect
     and one-time values. None of them is secret.
   - Log in with the Premium account and choose **Agree**.
4. The browser shows "Signed in. You can close this tab…". The terminal ends
   with one line:

   ```json
   {"result":"saved","path":"/run/user/1000/bhp-music/token/spotify-token.json"}
   ```

If it stops with an error instead (one JSON line on stderr, a code and a
message, never a token):

| Code | Meaning | What to do |
|---|---|---|
| `MUSIC.TOKEN_EXISTS` | That directory already holds a token | Finish Part 3 with it, or remove the directory and run again |
| `MUSIC.SIGN_IN_PORT_UNAVAILABLE` | Something else uses port 8888 | Stop it, or add a second redirect URI with another port and pass `--port` |
| `MUSIC.SIGN_IN_TIMEOUT` | No redirect within 5 minutes | Run again |
| `MUSIC.SIGN_IN_DENIED` | **Cancel** was chosen on Spotify's page | Run again and choose **Agree** |
| `MUSIC.SIGN_IN_STATE_MISMATCH` | The redirect was not this run's (an old tab) | Close old tabs and run again |
| `MUSIC.SIGN_IN_REFUSED` | Spotify refused the code; the message names the OAuth error | `invalid_client`: check the client ID. `invalid_grant`: run again |
| `MUSIC.SIGN_IN_SCOPE_MISSING` | Spotify granted fewer scopes | Run again and accept both |

If Spotify's own page says "INVALID_CLIENT: Invalid redirect URI", the
dashboard's redirect URI is not exactly `http://127.0.0.1:8888/callback`.

## Part 3: put the token on the panel, then delete the PC copy

The panel's music service runs as the reserved UID 1105 (`bhp-music`). On
the bench tier ([ADR 0014](../decisions/0014-run-a-bench-tier-on-the-vendor-image.md),
[frdm-bench.md](frdm-bench.md)) its state is in
`/opt/house-panel/state/music`, on the board's persistent storage outside
any image (ADR 0013).

**First, the console must be closed.** ADR 0014 puts no token on the board
while root has an empty password; [frdm-bench.md](frdm-bench.md), owner
step 1, closes it.

Writing the token is a board-storage action, so it is yours. Run these from
the PC with your own ssh key. The second line names the speaker exactly as
the Spotify app shows it:

```sh
ssh root@192.168.50.19 'install -d -m 700 -o 1105 -g 1105 /opt/house-panel/state/music /opt/house-panel/state/music/token'
scp -p "/run/user/$(id -u)/bhp-music/token/spotify-token.json" root@192.168.50.19:/opt/house-panel/state/music/token/
ssh root@192.168.50.19 'chown 1105:1105 /opt/house-panel/state/music/token/spotify-token.json && chmod 600 /opt/house-panel/state/music/token/spotify-token.json && ls -ln /opt/house-panel/state/music/token'
rm -r "/run/user/$(id -u)/bhp-music"
```

- The `ls -ln` line shows `-rw-------`, owner `1105`, and the file size. It
  never shows the contents.
- The board's address is the current DHCP lease
  ([AGENTS.md](../../AGENTS.md)).
- Deleting the PC copy matters. The panel saves every rotated token, but
  Spotify does not say whether an old token stops working.

Then tell the agent "Spotify sign-in done". It restarts the bench, and
`bhp-music-control` picks up the token. The agent never reads the token
file.

The client ID and the speaker's name are not secrets. They are already in
`/etc/house-panel/music.json` on the board
(`{"client_id": "...", "output_name": "Bathroom"}`), written by the agent on
2026-11-10 after Part 1.

## Revoking

Any one of these stops the panel:
- **Remove the panel's access:** at https://www.spotify.com/account/apps/
  find **House panel** and choose **Remove Access**. The panel then shows
  "sign in again" at its next refresh.
- **Delete the app** in the developer dashboard. That ends every token it
  issued.
- **Delete the file on the panel:**
  `ssh root@192.168.50.19 'rm /opt/house-panel/state/music/token/spotify-token.json'`.

An access token already issued keeps working until it expires, about an hour.
To sign in again later, repeat Parts 2 and 3. Part 1 stays done.
