#!/usr/bin/env bash
# Run on the Incus host (e.g. Alma WSL). Creates (if missing) an instance, mounts this repo at /opt/qmw, installs deps, builds training-wui.
# Example: ./setup-instance.sh /mnt/c/GitHub/LLM_Pract/qminiwasm-core
set -euo pipefail

# Override: INSTANCE=my-name ./setup-instance.sh ...
INSTANCE="${INSTANCE:-qminiwasm-training-wui}"
IMAGE="${IMAGE:-images:almalinux/10/cloud}"

if [[ $# -lt 1 ]]; then
  echo "usage: $0 /absolute/path/to/qminiwasm-core-repo-on-this-host" >&2
  exit 1
fi

REPO_HOST="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
if [[ ! -d "$REPO_HOST/engine" || ! -d "$REPO_HOST/qminiwasm" ]]; then
  echo "error: $REPO_HOST does not look like qminiwasm-core (missing engine/ or qminiwasm/)" >&2
  exit 1
fi

if ! command -v incus >/dev/null 2>&1; then
  echo "error: incus not found in PATH" >&2
  exit 1
fi

if ! incus info "$INSTANCE" &>/dev/null; then
  echo "Launching instance $INSTANCE from $IMAGE ..."
  incus launch "$IMAGE" "$INSTANCE"
  incus config set "$INSTANCE" boot.autostart true
  echo "Waiting for guest to be reachable..."
  for _ in $(seq 1 60); do
    if incus exec "$INSTANCE" -- true 2>/dev/null; then
      break
    fi
    sleep 2
  done
else
  echo "Instance $INSTANCE already exists; skipping launch."
fi

# Always ensure the instance starts when the Incus host boots (fixes older instances created before this was set).
incus config set "$INSTANCE" boot.autostart true

if incus config device show "$INSTANCE" 2>/dev/null | grep -q '^qmw-repo:'; then
  echo "Device qmw-repo already present; remove it first if you need a different source path."
else
  echo "Mounting host repo $REPO_HOST -> guest /opt/qmw"
  incus config device add "$INSTANCE" qmw-repo disk source="$REPO_HOST" path=/opt/qmw
fi

echo "Installing OS packages inside $INSTANCE (dnf)..."
incus exec "$INSTANCE" -- bash -s <<'EOS'
set -euo pipefail
if command -v dnf >/dev/null 2>&1; then
  dnf install -y curl unzip golang git python3-pip python3-devel gcc gcc-c++ make openssl-devel libffi-devel
elif command -v yum >/dev/null 2>&1; then
  yum install -y curl unzip golang git python3-pip python3-devel gcc gcc-c++ make openssl-devel libffi-devel
else
  echo "Neither dnf nor yum found in guest." >&2
  exit 1
fi
EOS

echo "Installing OpenTofu (tofu) to /usr/local/bin for RunPod / infra/runpod from the WUI..."
incus exec "$INSTANCE" -- bash /opt/qmw/training-wui/incus/install-opentofu.sh

echo "Allow TCP 8765 on guest (Alma cloud images often run firewalld) ..."
incus exec "$INSTANCE" -- bash -s <<'EOS'
set -euo pipefail
if command -v firewall-cmd >/dev/null 2>&1 && systemctl is-active --quiet firewalld 2>/dev/null; then
  firewall-cmd --permanent --add-port=8765/tcp || true
  firewall-cmd --add-port=8765/tcp || true
fi
EOS

echo "Building training-wui (Go) -> /opt/wui/training-wui (guest disk, not bind mount) ..."
incus exec "$INSTANCE" -- bash -s <<'EOS'
set -euo pipefail
cd /opt/qmw/training-wui
test -f go.mod && test -f main.go || {
  echo "error: /opt/qmw/training-wui missing go.mod/main.go — is qmw-repo mounted?" >&2
  exit 1
}
go version
mkdir -p /opt/wui
go build -o /opt/wui/training-wui .
test -x /opt/wui/training-wui
EOS

echo "Installing Python deps and package (CPU torch) — can take several minutes..."
incus exec "$INSTANCE" -- bash -s <<'EOS'
set -euo pipefail
cd /opt/qmw
for f in README.md pyproject.toml setup.py; do
  if [[ ! -f "$f" ]]; then
    echo "error: /opt/qmw/$f missing — is the repo bind-mount complete?" >&2
    exit 1
  fi
done
python3 -m pip install --upgrade "pip>=24.2" "setuptools>=69" wheel
python3 -m pip install torch --index-url https://download.pytorch.org/whl/cpu
python3 -m pip install -r requirements/docker.txt
python3 -m pip install "pydantic>=2.5" "datasets>=2.14"
# Editable metadata: use the same env as runtime deps (torch, qiskit, …). Isolated builds
# often fail with "Failed to build file:///opt/qmw when getting requirements to build editable".
python3 -m pip install -e . --no-deps --no-build-isolation
EOS

echo "Installing systemd unit so WUI starts on container boot..."
incus exec "$INSTANCE" -- bash -s <<'EOS'
set -euo pipefail
UNIT_SRC=/opt/qmw/training-wui/incus/training-wui.service
UNIT_DST=/etc/systemd/system/training-wui.service
if [[ ! -f "$UNIT_SRC" ]]; then
  echo "error: $UNIT_SRC missing — pull latest repo or add training-wui/incus/training-wui.service" >&2
  exit 1
fi
cp -f "$UNIT_SRC" "$UNIT_DST"
systemctl daemon-reload
systemctl enable training-wui.service
systemctl restart training-wui.service || systemctl start training-wui.service
systemctl --no-pager -l status training-wui.service || true
EOS

echo ""
echo "Done."
echo "WUI should be running and enabled on boot (systemd: training-wui.service)."
echo "Manual run (foreground):"
echo "  incus exec $INSTANCE -- bash -lc 'cd /opt/qmw/training-wui && go run . -root /opt/qmw -addr 0.0.0.0:8765'"
echo "Or binary:"
echo "  incus exec $INSTANCE -- bash -lc '/opt/wui/training-wui -root /opt/qmw -addr 0.0.0.0:8765'"
echo "Status / logs:"
echo "  incus exec $INSTANCE -- systemctl status training-wui"
echo "  incus exec $INSTANCE -- journalctl -u training-wui -n 50 --no-pager"
echo "Optional proxy on host port 8765:"
echo "  incus config device add $INSTANCE wui proxy listen=tcp:127.0.0.1:8765 connect=tcp:127.0.0.1:8765"
