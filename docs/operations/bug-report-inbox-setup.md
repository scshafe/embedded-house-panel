# Bug-Report Inbox: What Infra Installs on the PC

<!-- @canonical bug-report-inbox-setup -->

**Status (2026-11-08):** ready for the owner's oversight session (decision 11
of the accepted [bug-report design](../architecture/bug-reports-and-self-repair.md)).
Nothing here has been installed. This project supplies the code
([report_inbox/](../../report_inbox/README.md)). Everything below is the
fleet-side work only infra can do.
- `<PC>` is the PC's MagicDNS name, for example `cole-arch-pc.<tailnet>.ts.net`.
- `<PC_IP>` is its tailnet IPv4 (`tailscale ip -4`).
- `<COMMIT>` is the merged commit of this repository to install.

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
| `/srv/house-panel-reports/incoming/` | `bhp-report-inbox` `0700` | none |
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

  Renew it with the same command monthly, from a timer, then restart the
  unit.
- **Listening:** `<PC_IP>` port **8743**, TCP, on the tailnet address only,
  never `0.0.0.0`.
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
  --transcribe-command "<WHISPER_CLI> -m <WHISPER_MODEL> -f {input} -otxt -of {output_base}"
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

## 6. whisper.cpp (decision 4, the owner's install)

- **The binary and model:** `<WHISPER_CLI>` is whisper.cpp's command-line
  program, and `<WHISPER_MODEL>` an English model file (for example
  `ggml-base.en.bin`). Both are readable by `bhp-report-inbox`.
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
