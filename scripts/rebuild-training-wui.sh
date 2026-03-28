#!/usr/bin/env bash
# Build the Training WUI Go binary. Run from the repository root.
#
# Usage:
#   ./scripts/rebuild-training-wui.sh
#   ./scripts/rebuild-training-wui.sh --test   # go test -count=1 (no cache)
#   ./scripts/rebuild-training-wui.sh --generate
#   ./scripts/rebuild-training-wui.sh --test --generate
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WUI_DIR="$REPO_ROOT/training-wui"

DO_TEST=0
DO_GEN=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --test) DO_TEST=1 ;;
    --generate) DO_GEN=1 ;;
    *)
      echo "unknown option: $1" >&2
      exit 1
      ;;
  esac
  shift
done

cd "$WUI_DIR"

if ! command -v go >/dev/null 2>&1; then
  echo "error: go not on PATH (need Go 1.22+; see training-wui/README.md)" >&2
  exit 1
fi

if [[ "$DO_TEST" -eq 1 ]]; then
  echo "go test -count=1 ./... (cache disabled)" >&2
  go test -count=1 ./...
fi
if [[ "$DO_GEN" -eq 1 ]]; then
  go generate ./...
fi

go build -trimpath -o training-wui .

echo "Built: $WUI_DIR/training-wui"
echo "Run from repo root: \"$WUI_DIR/training-wui\" -root \"$REPO_ROOT\" -addr :8765"
