# Bug-Report Inbox: What Infra Installs on the PC

<!-- @canonical bug-report-inbox-setup -->

**Status (2026-11-08):** installed on the owner's PC by the oversight session
(decision 11 of the accepted [bug-report design](../architecture/bug-reports-and-self-repair.md)):
infra PR #61 (merge `5d5b44a`), with the receiver pinned to this repository's
`0438b27`; infra re-pins it to the merge of the receiver fixes. It listens on
`https://<PC>:8743`, with whisper.cpp v1.9.5 in `/opt/whisper.cpp`. The
panel side is not packaged yet. This project supplies the code
([report_inbox/](../../report_inbox/README.md)). Everything below is the
fleet-side work only infra can do.
- `<PC>` is the PC's MagicDNS name, for example `cole-arch-pc.<tailnet>.ts.net`.
- `<PC_IP>` is its tailnet IPv4 (`tailscale ip -4`).
- `<COMMIT>` is the merged commit of this repository to install.

**The install is infra's:** infra packages this code in its
`docs/platform/house-panel-report-inbox.md` (infra PR #61), pinned to a merged
commit of this repository. Install from that doc. This page states what the
code needs from the PC; where the two differ on a path or a flag, infra's doc
is what gets installed.

## 1. The principal

- **System user:** `bhp-report-inbox`, no login shell, no home of its own.
- **System group:** `bhp-reports`, with `bhp-report-inbox` as a member.
- **The agent user `agent-embedded-house-panel`** is not added to the group.
  It gets the ACLs in section 3 instead, so it can never touch `incoming/`
  or the TLS key.

```sh
groupadd --system bhp-reports
useradd --system --gid bhp-reports --no-create-home --shell /usr/bin/nologin bhp-report-inbox
```

## 2. The files to install

Install the code from a merged commit, root-owned and read-only:

```sh
install -d -m 755 /opt/house-panel-report-inbox
git -C /home/cole/src/embedded-house-panel archive <COMMIT> report_inbox/bhp_report_inbox \
  | tar -x -C /opt/house-panel-report-inbox --strip-components=1
chown -R root:root /opt/house-panel-report-inbox && chmod -R a+rX,go-w /opt/house-panel-report-inbox
```

That gives `/opt/house-panel-report-inbox/bhp_report_inbox/`, which needs
Python 3 only. Updating it means installing a newer merged commit the same
way and restarting the unit.

## 3. The inbox directory, its modes and the agent's ACLs

```sh
install -d -o bhp-report-inbox -g bhp-reports -m 2750 /srv/house-panel-reports
install -d -o bhp-report-inbox -g bhp-reports -m 0700 /srv/house-panel-reports/incoming
for d in reports fingerprints transcripts; do
  install -d -o bhp-report-inbox -g bhp-reports -m 2750 /srv/house-panel-reports/$d
done
install -d -o bhp-report-inbox -g bhp-reports -m 2770 /srv/house-panel-reports/status
# The agent: through the root, read the bundles, write statuses.
setfacl -m u:agent-embedded-house-panel:x /srv/house-panel-reports
for d in reports fingerprints transcripts; do
  setfacl -R -m u:agent-embedded-house-panel:rX /srv/house-panel-reports/$d
  setfacl -R -d -m u:agent-embedded-house-panel:rX /srv/house-panel-reports/$d
done
setfacl -R -m u:agent-embedded-house-panel:rwX /srv/house-panel-reports/status
setfacl -R -d -m u:agent-embedded-house-panel:rwX /srv/house-panel-reports/status
```

| Path | Owner, mode | Agent |
|---|---|---|
| `/srv/house-panel-reports/incoming/` | `bhp-report-inbox` plain `0700`, no setgid, no ACL | none |
| `/srv/house-panel-reports/reports/<id>/` | `bhp-report-inbox:bhp-reports` `0750`, files `0640` | read |
| `/srv/house-panel-reports/status/` | `2770` | read and write |
| `/srv/house-panel-reports/fingerprints/`, `transcripts/` | `2750` | read |

Nothing under `/srv/house-panel-reports` is ever in a Git repository or a
backup that leaves the house.

## 4. TLS and the address

- **The certificate** comes from `tailscale cert` for `<PC>`, readable by the
  receiver only:

  ```sh
  install -d -m 750 -g bhp-reports /etc/house-panel-report-inbox
  tailscale cert --cert-file /etc/house-panel-report-inbox/tls.crt \
    --key-file /etc/house-panel-report-inbox/tls.key <PC>
  chgrp bhp-reports /etc/house-panel-report-inbox/tls.* && chmod 640 /etc/house-panel-report-inbox/tls.*
  ```

  Renew it weekly from a timer, with the same command plus
  `--min-validity 504h` (a new certificate once fewer than 21 days remain),
  then restart the unit. A restart loses no transcription: on start, the
  receiver queues again every open report whose audio has no transcript.
- **Listening:** `<PC_IP>` port **8743**, TCP, on the tailnet address only,
  never `0.0.0.0`. `serve` refuses an `--address` that is empty, a wildcard
  or a host name.
- **Connections:** the receiver checks the peer (below) before the TLS
  handshake, and both run in the connection's own thread. A handshake, and
  then each read or write, gets 30 s. At most 8 connections are served at
  once; more are closed as soon as they are accepted. One idle connection
  cannot stall the inbox.
- **`tailscale whois`:** the receiver runs `tailscale whois --json` for each
  new peer address, cached for 60 s, and admits only nodes tagged
  `tag:house-panel`. Check that `bhp-report-inbox` can run it:
  `sudo -u bhp-report-inbox tailscale whois --json <PC_IP>`. If tailscaled's
  LocalAPI refuses that user, give it the narrowest access tailscaled offers
  for whois. Do not run the receiver as root.

## 5. The service

`/etc/systemd/system/bhp-report-inbox.service`:

```ini
[Unit]
Description=House panel bug-report inbox
After=tailscaled.service network-online.target
Wants=network-online.target

[Service]
User=bhp-report-inbox
Group=bhp-reports
UMask=0027
Environment=PYTHONPATH=/opt/house-panel-report-inbox
ExecStart=/usr/bin/python3 -m bhp_report_inbox --root /srv/house-panel-reports serve \
  --address <PC_IP> --port 8743 --allowed-tag tag:house-panel \
  --cert /etc/house-panel-report-inbox/tls.crt --key /etc/house-panel-report-inbox/tls.key \
  --transcribe-command "<WHISPER_CLI> -m <WHISPER_MODEL> -l en -t 4 -np -f {input} -otxt -of {output_base}"
Restart=on-failure
NoNewPrivileges=yes
ProtectSystem=strict
ProtectHome=yes
PrivateTmp=yes
ReadWritePaths=/srv/house-panel-reports

[Install]
WantedBy=multi-user.target
```

The unit's log (`journalctl -u bhp-report-inbox`) carries codes and report
IDs only.

**Retention (decision 9):** a daily timer runs, as `bhp-report-inbox`:
`python3 -m bhp_report_inbox --root /srv/house-panel-reports retention --days 90`.
It deletes:
- reports closed more than 90 days ago, with their statuses and transcripts;
- the audio of a closed report as soon as it has a transcript;
- uploads in `incoming/` abandoned for 7 days (`--upload-days`); the panel
  then sends the report again from the first byte;
- fingerprint records with no report left that were last seen more than
  90 days ago.

## 6. whisper.cpp (decision 4, the owner's install)

- **The binary and model:** whisper.cpp is installed under
  `/opt/whisper.cpp` (infra's doc gives the exact paths). `<WHISPER_CLI>` is
  its command-line program and `<WHISPER_MODEL>` an English model file (for
  example `ggml-base.en.bin`). Both are readable by `bhp-report-inbox`.
- **The flags:** `-l en` (English), `-t 4` (four threads) and `-np` (no
  progress or diagnostic output, so nothing about the audio reaches the
  unit's log).
- **The audio it gets:** the receiver checks each `description.wav` first:
  16-bit PCM, mono, 16 kHz, 60 s at most, which is what the panel sends.
  Anything else is logged as `REPORT_INBOX.AUDIO_FORMAT_INVALID` and never
  reaches whisper.cpp.
- **What it does:** the receiver runs it once per spoken description and
  writes `transcripts/<id>.txt`.
- **Without it:** leave `--transcribe-command` out, and the audio stays for
  the owner alone.
- **Locally only:** no transcription service in the cloud is used.

## 7. The tailnet grant

In the tailnet policy: `tag:house-panel` may reach `<PC>` on TCP 8743, and
nothing else on the PC. The panel's other grants are unchanged.

## 8. The panel side (for the later packaging step)

`bhp-reporter --inbox-url https://<PC>:8743`. The panel holds no secret for
the inbox; its tailnet node is its identity.

## 9. The broker sender (phase 2, later)

When automatic PR-only runs exist, the receiver's host asks the broker to file
one redacted `agent-request` on `scshafe/embedded-house-panel` per new
fingerprint:
- the sender is `from:house-panel-reports`;
- it is filed with `--public`, because the repository is public;
- the body is the output of
  `python3 -m bhp_report_inbox --root /srv/house-panel-reports public-summary <id> --markdown`
  and nothing else.

The automatic run then reads the bundle by its report ID from
`/srv/house-panel-reports`.

## Checking the install

As the agent user:
`PYTHONPATH=~/src/embedded-house-panel/report_inbox python3 -m bhp_report_inbox --root /srv/house-panel-reports list`
prints `[]` on an empty inbox. A `PermissionError` means the ACLs in section 3
are missing.
