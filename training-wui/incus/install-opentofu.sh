#!/usr/bin/env bash
# Install OpenTofu CLI as /usr/local/bin/tofu (Linux x86_64 / aarch64).
# Run on the Incus guest after curl+unzip are installed, or locally: sudo bash install-opentofu.sh
# Pin with: TOFU_VER=1.9.0 bash install-opentofu.sh
set -euo pipefail

TOFU_VER="${TOFU_VER:-1.9.0}"
ARCH="$(uname -m)"
case "$ARCH" in
  x86_64) TOFU_ARCH=amd64 ;;
  aarch64) TOFU_ARCH=arm64 ;;
  *)
    echo "error: unsupported architecture: $ARCH (expected x86_64 or aarch64)" >&2
    exit 1
    ;;
esac

for cmd in curl unzip; do
  command -v "$cmd" >/dev/null 2>&1 || {
    echo "error: $cmd not found — install it first (e.g. dnf install -y curl unzip)" >&2
    exit 1
  }
done

URL="https://github.com/opentofu/opentofu/releases/download/v${TOFU_VER}/tofu_${TOFU_VER}_linux_${TOFU_ARCH}.zip"
tmpdir="$(mktemp -d)"
trap 'rm -rf "$tmpdir"' EXIT

echo "Downloading OpenTofu v${TOFU_VER} (${TOFU_ARCH}) ..."
curl -fsSL "$URL" -o "$tmpdir/tofu.zip"
unzip -q "$tmpdir/tofu.zip" -d "$tmpdir/out"

bin="$(find "$tmpdir/out" -name tofu -type f 2>/dev/null | head -1)"
if [[ -z "$bin" ]]; then
  echo "error: could not find tofu binary inside release zip" >&2
  find "$tmpdir/out" -type f >&2 || true
  exit 1
fi

install -m 0755 "$bin" /usr/local/bin/tofu
export PATH="/usr/local/bin:${PATH}"
hash -r 2>/dev/null || true
echo "Installed: $(command -v tofu)"
tofu version
