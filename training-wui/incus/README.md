# Incus: training + WUI

The guest gets:

- Your repo bind-mounted at **`/opt/qmw`**
- Optional **`/opt/wui/training-wui`** — pre-built binary from `setup-instance.sh` (faster restarts)
- Python + this package under **`/opt/qmw`** (for `python -m engine …`)

## Setup (once per host)

```bash
cd /path/to/qminiwasm-core/training-wui/incus
chmod +x setup-instance.sh
./setup-instance.sh /path/to/qminiwasm-core
```

Use the real absolute path to the repo root (must contain `engine/` and `qminiwasm/`).

Optional: `INSTANCE=other-name` or `IMAGE=images:almalinux/10/cloud` in the environment.

## Autostart on container boot (recommended)

`setup-instance.sh` installs a **systemd** unit `training-wui.service` so the WUI starts after reboot and restarts on failure.

- Status: `incus exec qminiwasm-training-wui -- systemctl status training-wui`
- Logs: `incus exec qminiwasm-training-wui -- journalctl -u training-wui -f`

If you already ran setup before this existed, install once:

```bash
incus exec qminiwasm-training-wui -- bash /opt/qmw/training-wui/incus/install-systemd-wui.sh
```

(Requires repo mount at `/opt/qmw` and binary at `/opt/wui/training-wui`.)

## Run the WUI (manual / foreground)

Start the server and **leave this terminal open** — when you stop it or close the shell, the web UI is gone (unless systemd is running it).

```bash
incus exec qminiwasm-training-wui -- bash -lc 'cd /opt/qmw/training-wui && go run . -root /opt/qmw -addr 0.0.0.0:8765'
```

You should see a log line like `training-wui listening on 0.0.0.0:8765`. If the process exits immediately, scroll up for the error.

**After setup completed the Go build step** (binary exists):

```bash
incus exec qminiwasm-training-wui -- bash -lc '/opt/wui/training-wui -root /opt/qmw -addr 0.0.0.0:8765'
```

## Reach the UI from the Incus host

**`Connection refused` to the container IP on port 8765** means nothing accepted the TCP connection. Common cases:

1. **No server on that port** — the WUI must be running in **another** terminal and stay there until you see `listening on 0.0.0.0:8765`. If `incus exec … go run …` returns to a shell prompt, the process is gone and the port is closed.
2. **Guest firewall** — only if `firewalld` is **active** in the guest. Check: `incus exec qminiwasm-training-wui -- systemctl is-active firewalld` → must print `active`. If it prints `inactive`, skip this; your port is not blocked by firewalld. When active, open the port once:  
   `incus exec qminiwasm-training-wui -- bash -lc 'firewall-cmd --permanent --add-port=8765/tcp && firewall-cmd --add-port=8765/tcp'`  
   (`setup-instance.sh` does this automatically when firewalld is running.)

Check for a listener **in the guest** while the WUI should be up:

```bash
incus exec qminiwasm-training-wui -- ss -tln | grep 8765
```

Expect `LISTEN` on `0.0.0.0:8765` or `*:8765`. No output → start the WUI first.

From the host (IP matches `incus list`):

```bash
ip=$(incus exec qminiwasm-training-wui -- hostname -I | awk '{print $1}')
curl -v "http://${ip}:8765/"
```

From inside the guest only:

```bash
incus exec qminiwasm-training-wui -- curl -sS -o /dev/null -w "%{http_code}\n" http://127.0.0.1:8765/
```

If **`ip` is empty**, the guest has no IPv4 yet (`hostname -I` blank).

### Loopback on the host vs Windows

If Incus runs in **WSL**, `127.0.0.1` on **Windows** is not WSL’s loopback. Use the proxy below, or open the UI from WSL, or use the WSL-facing IP from Windows.

### Proxy device (optional)

Forwards a port on the machine that runs `incus` into the container (remove an existing `wui` device first if you hit “already exists”):

```bash
incus config device remove qminiwasm-training-wui wui 2>/dev/null || true
incus config device add qminiwasm-training-wui wui proxy listen=tcp:127.0.0.1:8765 connect=tcp:127.0.0.1:8765
```

Then from **that same host**: `curl -v http://127.0.0.1:8765/`

To accept connections from other interfaces on the Incus host (e.g. reach WSL’s port from Windows), use `listen=tcp:0.0.0.0:8765` instead — only for trusted networks.

### If `go run` or `go build` fails

- **`go: go.mod requires go >= 1.22`**: install a newer Go in the guest.
- **`missing go.mod` under `/opt/qmw/training-wui`**: fix the `qmw-repo` disk (`incus config device show qminiwasm-training-wui`).

## Environment / `.env`

Secrets and defaults (IBM token, HF token, `ACCELERATOR`, etc.) should live in **`.env` at the repo root** on the host — the same file appears as **`/opt/qmw/.env`** in the guest via the bind mount.

The `training-wui` process **loads that file at startup** (after `-root` resolves), so tokens apply to preflight and to `python -m engine` subprocesses even when the server is started by **systemd** (no manual shell `export`).

## Requirements

- `incus` on the host, permission to run instances
- Enough disk/RAM for CPU PyTorch in the guest
- Guest **Go ≥ 1.22** for this module (`training-wui/go.mod`)
