# Music to an External Speaker

<!-- @canonical music-to-external-speaker -->

**Status (2026-11-06):** a design, asked for by the owner. Nothing is built,
installed, flashed or paired, and the panel has not contacted the speaker.
The panel sends music to the owner's Bose Portable Home Speaker. The mirror
unit's own speaker ([mirror-unit-speaker-and-gate.md](../hardware/mirror-unit-speaker-and-gate.md))
stays for voice and chimes only. Every board action this needs is listed under
[Asks for the owner](#asks-for-the-owner).

Values are labelled by where they come from:
- **vendor:** Bose's owner's guide or product page, Spotify's developer
  documentation, the u-blox datasheet;
- **source:** a project's own code or documentation, at the named version;
- **board:** read from the board on 2026-11-06 over ssh, read-only
  (`hardware_tests/results/frdm/music-output-inventory-20261106/`, ignored by
  git; `inventory.txt` sha256 `04f6ba21…0cd`, `inventory-2.txt` `16af5426…710`,
  `inventory-3.txt` `3577323b…d2f`, `inventory-4.txt` `8cb7be7a…dd5`);
- **report:** a forum post or a user's issue, which is weaker evidence;
- **inference:** reasoned here and not measured.

## Recommendation

**Spotify Connect, with the panel as a remote control only.** The speaker
fetches and plays the stream itself, and the panel tells it what to play
through Spotify's Web API.

- **No audio leaves the panel.** Music cannot reach the microphone array,
  because there is no music audio on the panel at all.
- **Nothing to install** on the vendor image or the project image. It is
  HTTPS calls through the existing `shared/http_runtime` (libcurl, `GET`,
  `POST`, `PUT`).
- **The panel may sleep while music plays.** Suspend does not stop the
  speaker, so `panel_supervisor` needs no new power-policy input.
- **No shared LAN needed.** The panel and the speaker never talk to each
  other directly. The panel only needs internet on Wi-Fi or Ethernet.
- **Latency does not matter:** a control call takes well under a second and
  the stream does not pass through the panel.

**It only fits if the music is Spotify and the owner has Premium.**
Spotify's Player API "only works for users who have Spotify Premium", and a
development-mode app needs a Premium owner (vendor). It also adds a second
long-lived secret on the panel, a Spotify refresh token, which needs its own
ADR ([Secrets](#secrets)).

**If the owner wants music that is not Spotify** (their own files, internet
radio), the second choice is **AirPlay 2 through OwnTone**. It plays any
source and is already proven against AirPlay 2 receivers. The cost is a
custom package that neither Yocto nor Buildroot carries. The panel then also
has to stay awake while music plays. Not recommended:
- **PipeWire's RAOP sink:** released PipeWire sends only AirPlay 1, and
  nobody has shown this speaker accepting it.
- **Bluetooth:** 9 m range, one source at a time, and a shared 2.4 GHz
  antenna with the panel's Wi-Fi.

## The speaker

Bose Portable Home Speaker, sold earlier as the Portable Smart Speaker. From
the owner's guide (vendor; sha256 `5692182c…0ba`) unless marked:

- **AirPlay:** "The speaker is capable of playing AirPlay 2 audio" (iOS 11.4
  or later). The guide says nothing about AirPlay 1.
- **Bluetooth:** 4.2 and a 30 ft (9 m) range (product page). It stores "up to
  eight devices" but plays "from only one device at a time". Firmware 8.0.16
  (June 2021) added AAC; reviews report SBC and AAC only (report).
- **Spotify:** the guide names Spotify and Pandora as the supported default
  services and carries Spotify's licence notice.
- **Also:** Chromecast built-in since firmware 10.0.18 (product page). The
  owner did not ask about it and it is not compared here.
- **Network standby:** after 20 minutes with no audio, no button and no voice
  assistant, it goes into network standby (≤ 2.0 W with Wi-Fi and Bluetooth
  on). It wakes on a button press, on "Play or resume audio using your mobile
  device or using the Bose Music app", or on the voice assistant.
- **Auto-off:** "If the speaker isn't connected to a Wi-Fi network, streaming
  audio with Bluetooth technology, or charging, it powers off automatically
  after 20 minutes of inactivity." Once off, nothing on the network wakes it.
- **Battery:** 12 h of playback and "over 24 hours in standby mode" (product
  page); a review measured 13 h 6 min (report).
- **No local control API.** pybose (report, reverse-engineered) uses a
  websocket on port 8082 but needs a token from Bose's cloud. Bose's
  SoundTouch shutdown (May 2026) applies to SoundTouch products, not this
  one.

## What the board has today

The vendor image ("NXP i.MX Distro with Wayland and XWayland 6.18-wrynose",
kernel 6.18.20-2.0.0), read on 2026-11-06 (board):

| Piece | State |
|---|---|
| PipeWire | 1.6.3, `pipewire.service` running as user `pipewire`; `raop-discover` and `raop-sink` modules installed, with `50-raop.conf` under `pipewire.conf.avail` |
| WirePlumber | 0.5.14 installed, disabled and inactive |
| BlueZ | 5.86; `bluetooth.service` enabled but not running, because its condition `/sys/class/bluetooth` does not exist |
| IW612 Bluetooth | device-tree node `nxp,88w8987-bt` on UART `30860000`, status `okay`; `btnxpuart.ko` present but not loaded, so there is no `hci0` |
| Bluetooth audio | PipeWire's `bluez5` plugin with SBC, aptX, LC3, Opus and others, but no AAC; bluealsa 4.3.0 installed, disabled |
| GStreamer | 1.28.1 with `souphttpsrc`, mp3, FLAC and Vorbis decoders; no Opus or AAC decoder, no FFmpeg |
| avahi | `avahi-daemon` 0.8 running; `avahi-browse` absent |
| Network | connman 2.0 manages Ethernet only; `eth0` and `eth1` up on the bench LAN; Wi-Fi `mlan0` down; `wpa_supplicant` disabled |
| HTTPS | libcurl 8.19.0, OpenSSL 3.5.6 |
| OwnTone, librespot, FFmpeg, libplist, libconfuse | absent |
| ALSA | the array is card 0 `L16K6Ch` here (card 6 at the 2026-10-19 capture); the card number is not stable |

The project's Buildroot defconfigs (pinned 2026.05.1) select no PipeWire,
BlueZ or avahi today. Buildroot's master tree carries PipeWire, WirePlumber,
bluez5_utils, bluez-alsa, avahi, libplist and libsodium, but no OwnTone or
librespot (source; the versions at the 2026.05.1 pin are not checked).

## Options

### 1. AirPlay 1 through PipeWire's RAOP sink

**How:** `module-raop-discover` browses `_raop._tcp` over mDNS and creates a
sink for each receiver. A player then writes PCM to that sink.

- **On the vendor image:** all of it is installed (board). It needs a
  PipeWire instance that loads the module and a player linked to the sink. A
  private instance could run from `/tmp`, with nothing installed.
- **Protocol:** RAOP only. In 1.6.3 the encryption is `none`, `RSA`,
  `auth_setup` (or the undocumented `fp_sap25`), and the codec is PCM or
  ALAC (source, `module-raop-sink.c` at 1.6.3). AirPlay 2 pairing landed on
  PipeWire's master branch on 2026-10-21 and is in no release yet (source).
- **Will this speaker accept it? Unknown.**
  - A 2019 log from a Bose Soundbar 700, the same Bose Music firmware family,
    shows `_raop._tcp` with `et=0,4` (none, or auth-setup) and `sf=0x4` (no
    PIN), but PulseAudio got `403 Forbidden` (report).
  - pyatv's RAOP sender played to a Soundbar 700 until about 2026-10-09.
    Then the soundbar began dropping the connection after progress metadata
    (report).
  - This speaker's own record is unmeasured.
- **Latency:** 1.5 s default (source, `DEFAULT_LATENCY_MS 1500`). That is
  fine for music.
- **Risk to the array:** a system-wide PipeWire with WirePlumber would also
  own the array's ALSA card, and one wrong link sends music there. It would
  need a dedicated instance with no ALSA device at all
  ([No music through the array](#no-music-through-the-array)).

### 2. AirPlay 2 through OwnTone

**How:** OwnTone is a music server. It plays its library, HTTP streams or a
named pipe to AirPlay 2 speakers. A JSON API lists outputs and sets each one
on or off and its volume (`GET /api/outputs`, `PUT /api/outputs/{id}`), and
drives play, pause and the queue.

- **Versions:** 29.3 (2026-07-22) is current. AirPlay 2 came in 27.3 and has
  been the default since 29.1. AirPlay 1 stays as a fallback (source).
- **This speaker:** an issue reports "three airplay2 compatible speakers
  (Bose, Sonos and Naim) … play without issues" (report, 2021, Bose model not
  named). It has not been tested against this model.
- **On the vendor image:** absent, and no Yocto layer carries it (source).
  It needs a cross-build (none on this PC) and an install, which is an owner
  ask. Present: libevent, libgcrypt, json-c, SQLite, libsodium, libunistring,
  libxml2, libcurl. Absent: FFmpeg, libplist, libconfuse. protobuf-c was not
  checked.
- **On the project image:** a new Buildroot package, plus FFmpeg and
  libconfuse.
- **Its Spotify input does not help:** OwnTone's docs say it "doesn't work
  if you have a Spotify account created after around November 2025"
  (source).
- **Latency:** about 2 s for realtime AirPlay 2 audio (source,
  shairport-sync's notes). Fine for music; a pause takes effect after it.
- **Panel sleep:** the panel is the source, so suspend cuts the music
  ([Panel sleep](#panel-sleep)).

### 3. Bluetooth A2DP through the IW612

**How:** the panel pairs with the speaker as an A2DP source, through
PipeWire's bluez5 monitor (WirePlumber) or bluealsa, not both: they compete
for the same endpoints (inference).

- **On the vendor image:** BlueZ, PipeWire's bluez5 plugin, WirePlumber and
  bluealsa are all installed (board). The controller is not up:
  - `btnxpuart` is not loaded, so there is no `hci0`;
  - `bluetooth.service` skips itself;
  - pairing writes keys to `/var/lib/bluetooth` on the eMMC.
  Each of these is an owner ask.
- **Codec:** SBC. The speaker also takes AAC, but the vendor image has no
  AAC encoder for PipeWire (board).
- **Range:** 9 m (vendor), through whatever lies between the mirror and the
  speaker.
- **One source at a time.** While the panel holds the link, the owner's phone
  cannot play to the speaker over Bluetooth (vendor).
- **Radio:** the MAYA-W276 has one antenna for Wi-Fi, Bluetooth and 802.15.4.
  "Bluetooth, 802.15.4, and 2.4 GHz Wi-Fi are time-shared" (vendor, u-blox
  MAYA-W2 datasheet R07). With the panel on 2.4 GHz Wi-Fi, A2DP and the
  panel's own traffic take turns; on 5 GHz they do not (inference).
- **Latency:** about 100–275 ms for SBC (report). Lowest of the options; it
  does not matter for music.
- **Panel sleep:** as for AirPlay, suspend drops the link.
- **The speaker's power:** streaming over Bluetooth keeps it on (vendor). On
  battery and away from Wi-Fi it powers off 20 minutes after the music stops,
  and a powered-off speaker cannot be reconnected from the panel.

### 4. Spotify Connect, with the panel controlling it

**How:** the speaker is a Spotify Connect device on the owner's account. The
panel calls Spotify's Web API as the owner (vendor):
- `GET /v1/me/player/devices` (scope `user-read-playback-state`) lists the
  account's devices.
- `PUT /v1/me/player` with `device_ids` (scope `user-modify-playback-state`)
  moves playback there.
- `PUT /v1/me/player/play`, `/pause` and `/volume` control it.

Spotify's February 2026 changes kept these Player endpoints (vendor).

- **On either image:** nothing new. These are HTTPS requests through
  `shared/http_runtime` (`GET`, `POST`, `PUT`; the `Authorization` header is
  in its allowed set).
- **The app:** a Spotify developer app in development mode, owned by the
  owner. That allows "up to 5 authenticated Spotify users" on an allowlist,
  and "the app owner must have a Spotify Premium account". Extended quota is
  only for organisations, so development mode is permanent here (vendor).
- **Sign-in:** Authorization Code with PKCE, a public client with no client
  secret. Redirect URIs must be HTTPS or an explicit loopback literal
  (`http://127.0.0.1:PORT`), and `localhost` is refused (vendor). The panel
  has no browser, so the owner signs in once on a PC, and the refresh token
  is put on the panel outside the image, as in ADR 0012. A refresh "might"
  return a new refresh token, and the client keeps whichever is current
  (vendor).
- **Device visibility:** "Some device models are not supported and will not
  be listed" (vendor). Sleeping devices are often missing from the list until
  something wakes them (report).
  - Whether this speaker is listed in network standby, and whether a
    transfer wakes it, is **unmeasured**.
  - It is the first bench check ([Measurements](#measurements-before-building)).
- **Restricted devices:** if `is_restricted` is true, "no Web API commands
  will be accepted" (vendor). The UI then shows the speaker as unavailable.
- **Not usable:** the local ZeroConf `addUser` path is "available only for
  approved partners" (vendor).
- **Latency:** only the control call, under a second (inference). Spotify
  plays at its own quality, on the speaker.

## Comparison

| | 1. PipeWire RAOP | 2. OwnTone AirPlay 2 | 3. Bluetooth A2DP | 4. Spotify Connect |
|---|---|---|---|---|
| Speaker accepts it | unknown (AirPlay 1) | yes, AirPlay 2 (vendor) | yes (vendor) | yes (vendor); listing in standby unmeasured |
| Vendor image | installed; needs a private instance | absent: cross-build and install | installed; controller down; pairing writes eMMC | nothing new |
| Project image | add PipeWire, avahi | new package, FFmpeg, libconfuse | add BlueZ, PipeWire or bluealsa, firmware | nothing new |
| Sources it can play | anything the panel decodes | anything | anything | Spotify only, Premium |
| Panel and speaker on one LAN | yes (mDNS) | yes (mDNS) | no; within 9 m | no; internet only |
| Latency | 1.5 s | about 2 s | about 0.1–0.3 s | control only |
| Panel can sleep while playing | no | no | no | yes |
| Local audio on the panel | yes | yes, network only | yes, the radio | none |
| New secret on the panel | none | none (pairing state) | pairing keys | Spotify refresh token |
| Blocks the owner's phone | no | no | yes, one source | no |

## Cross-cutting design

### No music through the array

The array is the gated microphone device. Its USB playback feeds the mirror
speaker and the echo canceller's reference, and the media broker is its only
owner. Music never goes there, by construction rather than by routing rules:

- **One music principal, its own UID** (working name `music_control`). It
  holds no `bhp-mic` group, no `audio` group and no ACL on any `/dev/snd`
  node.
  - With Spotify Connect it opens no audio device at all.
  - With OwnTone, only its network outputs are built and enabled: no ALSA,
    PulseAudio or PipeWire output. A dedicated PipeWire instance for RAOP
    would load no ALSA monitor.
- **The broker never carries music.** `media_broker` serves voice purposes,
  and music is not a purpose there. The broker's playback path (with the
  [low-cut filter](../hardware/mirror-unit-speaker-and-gate.md#playback-filter))
  is for voice prompts and chimes.
- **The gate stays as it is.** The privacy switch and the PIR gate cut the
  array's 5 V. Music does not depend on the array being powered, so the gate
  needs no exception.
- **Voice while music plays.** The array's echo canceller has no reference
  for the Bose, so loud music reaches the microphones as noise. Before a
  voice lease (`voice_take`, later command recognition) the UI asks
  `music_control` to pause or lower the music, and restores it after `ended`.
  Spotify's `/pause` and `/volume`, or OwnTone's, do this. How much
  reduction recognition needs is unmeasured.

### How the panel UI picks the output

- **A music surface beneath `panel_ui`,** a sibling of vocabulary and agenda.
  It sends closed intents (`music.output.select`, `music.play`,
  `music.pause`, `music.volume`) to `music_control` over a Unix socket. As
  the panel's other sockets do, it authenticates the exact peer UID from
  kernel credentials.
- **Outputs come from configuration, not discovery.** The owner names the
  speaker once (its Spotify device name, or its AirPlay or Bluetooth
  identity). The UI lists only that output and "Off". The mirror speaker is
  never a music output.
- **Availability is a structured state.** For example
  `MUSIC.OUTPUT_UNAVAILABLE` (asleep, off or not listed),
  `MUSIC.OUTPUT_RESTRICTED`, `MUSIC.AUTH_REQUIRED` (refresh refused) and
  `MUSIC.NOT_PREMIUM`. The UI shows these as they are and never pretends the
  speaker is playing.
- **No automatic start.** Music plays only after a touch on the music
  surface. The last chosen output is remembered for the next touch, not
  replayed on boot or wake.
- **Ownership.** If the owner accepts this design, a registry row in
  [application-capability-ownership.md](application-capability-ownership.md)
  and an egress row in the [threat model](../security/threat-model.md)
  follow. Neither is added here.

### Wi-Fi or Ethernet

- **The installed mirror is on Wi-Fi.** Ethernet is for the bench only
  (packaging plan). Enrolling the panel in Wi-Fi is owner-gated and has not
  been done.
- **Spotify Connect** needs only internet, so any network with internet will
  do. The speaker stays on its own Wi-Fi as set up in the Bose Music app.
- **AirPlay** needs the panel and the speaker on one multicast-capable
  segment for mDNS, plus RTSP and RTP between them. The bench LAN
  (192.168.50.0/24) may not be the speaker's network. Streaming over Wi-Fi
  uses the panel's own airtime, which matters little at about 1.4 Mbit/s of
  PCM (inference).
- **Bluetooth** needs no network, but 2.4 GHz Wi-Fi shares its antenna (see
  above). Prefer 5 GHz for the panel if Bluetooth is used.

### Panel sleep

`panel_supervisor` suspends to RAM after a vacancy
([power-domain architecture](power-domain-architecture.md)).
- **With Spotify Connect** nothing changes: the panel can sleep, and the
  music keeps playing on the speaker. Someone at the panel wakes it to
  control the music again.
- **With AirPlay or Bluetooth** the panel is the source, so suspend stops
  the music. The supervisor, the only owner of power policy, would need a
  bounded "music playing" input that holds off suspend, and the power budget
  changes.

### The portable speaker's battery and sleep

- **In network standby** (on Wi-Fi, ≤ 2.0 W, about a day on battery) the
  speaker can be woken by "play or resume … using your mobile device or the
  Bose Music app" (vendor). Whether a Spotify Web API transfer or an AirPlay
  connection counts is unmeasured.
- **Off** (battery, no Wi-Fi, 20 minutes idle), nothing wakes it remotely.
  The UI reports `MUSIC.OUTPUT_UNAVAILABLE` and asks for the speaker's power
  button.
- **On its charging base** it does not auto-off (vendor). That is the most
  reliable place for it to be a panel output.
- **Battery level:** the panel cannot read it without Bose's cloud
  (report), so it shows none.

### Secrets

- **Spotify:** the refresh token acts for the owner's Spotify account. It
  would be the panel's second long-lived secret, after
  [ADR 0012](../decisions/0012-keep-a-rotating-voice-lab-refresh-token-on-the-panel.md).
  It follows the same rules:
  - one holder, `music_control`, with the token in a `0600` file outside
    the image;
  - a rotated token is written durably before use;
  - never in logs, arguments, URLs, the environment or the UI.
  It needs its own ADR, written once the owner chooses this option. The
  client ID is not secret.
- **AirPlay:** pairing state (OwnTone) and Bluetooth link keys are secrets
  too, kept in the same way under the music principal's UID.

## Measurements before building

None of these has run; each needs the owner ([Asks](#asks-for-the-owner)).

1. **Spotify device list.** With the speaker playing, idle for 20 minutes
   (network standby), and on battery away from its base: is it in
   `GET /v1/me/player/devices`, is `is_restricted` false, and does
   `PUT /v1/me/player` wake it and start playback?
2. **The speaker's AirPlay record** (`_raop._tcp` and `_airplay._tcp` TXT),
   if an AirPlay option stays in play. It decides whether option 1 can work
   at all.
3. **Ducking.** The voice-take level with music playing at a normal volume,
   paused, and lowered to about a third.

## Asks for the owner

Decisions:
- **Is the music Spotify, and is the account Premium?** If yes, option 4. If
  not, or if other sources matter too, option 2 as well or instead.
- **The Spotify developer app:** create it on the owner's account in
  development mode, with a loopback redirect URI, and allowlist the owner's
  account.
- **A new ADR** for the panel's Spotify refresh token (second exception to
  ADR 0010).

Board and network actions (none started; each is owner-gated or outside what
the agent may reach):
- **Option 4:** the panel's Wi-Fi enrolment for the installed mirror (an
  existing gate), and putting the refresh token on the panel after the
  owner's one-time sign-in. Nothing is installed.
- **Measurement 2:** a query of the LAN's mDNS for the speaker's record. The
  agent may not contact other LAN hosts. The owner can run
  `avahi-browse -rt _raop._tcp` from a PC on the speaker's network instead
  (the board has no `avahi-browse`).
- **Option 1:** a private PipeWire instance run from `/tmp` (no install) that
  connects to the speaker on the LAN.
- **Option 2:** installing an OwnTone build on the board; a cross-compiler on
  the PC to build it.
- **Option 3:** loading `btnxpuart`, starting `bluetooth.service`, and pairing
  (link keys written to `/var/lib/bluetooth` on the eMMC).

## Sources

Read 2026-11-06.

- Bose Portable Smart Speaker owner's guide (837954), sha256
  `5692182c1b89f5cd89a95c927180fcde1f44a1917e6c72860bb2d5c2f64ec0ba`:
  https://assets.bosecreative.com/m/5b18bf3aa00fa3d8/original/837954_OG_PORTSS-SPEAKERWIRELESS_enUS.pdf
- Bose product page: https://www.bose.com/p/speakers/bose-portable-smart-speaker/PORTSS-SPEAKERWIRELESS.html
- Bose firmware versions: https://support.bose.co.uk/article/portable-smart-speaker-software-and-firmware-versions
- Bose SoundTouch end of life: https://bose.com/soundtouch-end-of-life
- PipeWire `module-raop-sink.c` at 1.6.3: https://raw.githubusercontent.com/PipeWire/pipewire/1.6.3/src/modules/module-raop-sink.c
- PipeWire AirPlay 2 pairing commit (master): https://api.github.com/repos/PipeWire/pipewire/commits/9d3a4136
- Soundbar 700 RAOP record and 403: https://bbs.archlinux.org/viewtopic.php?id=250086
- pyatv RAOP against a Soundbar 700: https://github.com/JVenberg/RaspiAirplay/pull/1
- OwnTone changelog, AirPlay, building, Spotify, JSON API:
  https://owntone.github.io/owntone-server/changelog/ ,
  https://owntone.github.io/owntone-server/audio-outputs/airplay/ ,
  https://owntone.github.io/owntone-server/building/ ,
  https://owntone.github.io/owntone-server/integrations/spotify/ ,
  https://owntone.github.io/owntone-server/json-api/
- OwnTone report with a Bose speaker: https://github.com/owntone/owntone-server/issues/1369
- shairport-sync on AirPlay latency: https://github.com/mikebrady/shairport-sync/blob/master/README.md ,
  https://github.com/mikebrady/shairport-sync/blob/master/AIRPLAY2.md
- Spotify Web API: https://developer.spotify.com/documentation/web-api/reference/transfer-a-users-playback ,
  https://developer.spotify.com/documentation/web-api/reference/get-a-users-available-devices ,
  https://developer.spotify.com/documentation/web-api/concepts/quota-modes ,
  https://developer.spotify.com/documentation/web-api/concepts/redirect_uri ,
  https://developer.spotify.com/documentation/web-api/tutorials/refreshing-tokens ,
  https://developer.spotify.com/documentation/web-api/references/changes/february-2026
- Spotify ZeroConf: https://developer.spotify.com/documentation/commercial-hardware/implementation/guides/zeroconf
- Spotify sleeping devices (report): https://community.spotify.com/t5/Spotify-for-Developers/v1-me-player-devices-returns-empty-array/m-p/5224904
- pybose: https://pypi.org/project/pybose/
- u-blox MAYA-W2 datasheet: https://content.u-blox.com/sites/default/files/documents/MAYA-W2_DataSheet_UBX-22009721.pdf
- SBC latency (report): https://www.rtings.com/headphones/learn/latency
- SoundGuys review (report): https://www.soundguys.com/bose-portable-smart-speaker-review-75589/
