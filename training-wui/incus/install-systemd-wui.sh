#!/usr/bin/env bash
# Run inside the guest (or: incus exec INSTANCE -- bash -s < install-systemd-wui.sh)
# Installs/enables training-wui.service so the WUI survives container reboots.
set -euo pipefail
UNIT_SRC="${UNIT_SRC:-/opt/qmw/training-wui/incus/training-wui.service}"
UNIT_DST=/etc/systemd/system/training-wui.service
if [[ ! -f "$UNIT_SRC" ]]; then
  echo "error: $UNIT_SRC not found (is repo mounted at /opt/qmw?)" >&2
  exit 1
fi
if [[ ! -x /opt/wui/training-wui ]]; then
  echo "error: /opt/wui/training-wui missing or not executable — run setup-instance.sh build step first" >&2
  exit 1
fi
cp -f "$UNIT_SRC" "$UNIT_DST"
systemctl daemon-reload
systemctl enable training-wui.service
systemctl restart training-wui.service
echo "training-wui.service enabled and started."
systemctl --no-pager -l status training-wui.service
