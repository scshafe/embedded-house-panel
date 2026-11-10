# Music Control

<!-- @canonical music_control -->

The panel's remote for music on the owner's Bose speaker, through Spotify
Connect (#69, [ADR 0013](../docs/decisions/0013-keep-a-spotify-refresh-token-on-the-panel.md)).
The speaker fetches and plays the stream itself. This domain sends Spotify Web
API commands and never carries audio: it opens no audio device and has no
microphone access ([design](../docs/architecture/music-to-external-speaker.md)).

## Pieces

- **`spotify_auth.h`** (`@canonical spotify_refresh_token_store`,
  `@canonical spotify_session`): Authorization Code with PKCE for the owner's
  development-mode app. It is a public client with no client secret and a
  loopback redirect.
  - `start_pkce` makes the verifier, the state and the authorize URL.
  - `authorization_code` checks the redirect's state before anything else.
  - `exchange_code` needs a refresh token and both playback scopes.
  - `SpotifyTokenStore` keeps `spotify-token.json` (0600, owner-only
    directory) with the client ID it belongs to.
  - `SpotifySession` refreshes single-flight. A returned refresh token is
    written durably before the new access token is used. A failed write or
    `invalid_grant` signs out, and `invalid_grant` clears the file.
- **`spotify_client.h`** (`@canonical spotify_web_api_client`): the Player
  endpoints only.
  - Reads: devices and the playback state.
  - Commands: transfer, play, pause, next and volume, each with a
    `device_id`.
  - A 401 refreshes once and retries. Every other failure is a `MUSIC.*` code
    (the header lists them).
- **`music_protocol.h`** (`@canonical music_control_protocol`,
  `@canonical music_control_client`): the UI's socket.
  - Framing: one JSON object per line, closed key sets, exact peer UID both
    ways, frames up to 16 KiB.
  - The UI sends `status`, `select_output`, `play`, `pause`, `next` and
    `volume`.
  - The service answers `state` or `refused`.
  - No device ID or token crosses it.
- **`music_service.h`** (`@canonical music_control_service`): the one
  configured speaker, matched by its exact Spotify device name.
  - It re-reads the device list and the playback state every 15 s, and
    1.5 s after a command, because Spotify does not order player commands.
  - Retryable failures back off from 5 s to 5 minutes.
  - Track text is cut to 256 bytes on a UTF-8 boundary.
  - A token file copied in while it runs is read on the next tick, within
    5 s when signed out, with no restart. It compares the file's inode, size
    and modification time with the last copy it read or wrote.
- **`bhp-music-control`**: the service process, under its own UID (reserved
  as `bhp-music`, 1105, in `system_image/service-principals.json`).
  - Usage: `--ui-socket PATH --ui-uid UID --state-dir DIR --client-id ID
    --output-name NAME`. The token is read from `DIR/token/spotify-token.json`.
  - It logs codes only: never a token, a device ID, a track or the speaker's
    name.
- **`bhp-music-spotify-sign-in`**: the owner's one-time sign-in on the PC.
  - Usage: `--client-id ID --token-dir DIR [--port 8888]`.
  - It listens on `127.0.0.1` only and writes the token straight to the file.
  - It prints the authorize URL and, at the end, the file's path. It never
    prints the token, the code or the verifier.
  - The steps are in
    [spotify-owner-setup.md](../docs/operations/spotify-owner-setup.md).

## Tests

No test calls Spotify. The fixtures in `tests/fixtures/spotify/` follow the
examples in Spotify's Web API reference (their README says which), with
invented names and IDs.

- `music_control_protocol_test`: the frames and the client's UID check.
- `music_control_spotify_auth_test`:
  - the RFC 7636 S256 vector and the PKCE URL;
  - redirect parsing;
  - the code exchange (exactly the PKCE fields, no Authorization header);
  - the store's modes;
  - rotation saved before use;
  - single-flight;
  - `invalid_grant`;
  - a save that fails.
- `music_control_spotify_client_test`: each command's method and path, the
  fixture reads, and the failure mapping, including the 401 retry.
- `music_control_music_service_test`:
  - the state the UI sees;
  - commands reaching only the configured speaker;
  - refusals: missing, restricted, no volume control;
  - backoff and sign-out;
  - a state frame carrying no device ID or token.
- `music_control_sign_in_tool_test`: the sign-in tool end to end.
  - A loopback fake token endpoint checks the PKCE exchange.
  - The script plays the browser.
  - It checks the 0600 file and that nothing the tool printed holds the
    token, the code or the verifier.
- `music_control_daemon_usage_test`.

Run them with `ctest --test-dir .build/host-dev -R music_control`.

## The UI, and what is not done yet

- **The panel UI's music surface:** `panel_ui`'s `music` app and the
  simulator's client ([simulator/README.md](../simulator/README.md#music)).
- **Board:** nothing is on the board. The service runs there only after the
  owner's sign-in, and with the owner's go-ahead.
- **Voice:** pausing or lowering the music during a voice take, as the design
  describes, is not built yet.
