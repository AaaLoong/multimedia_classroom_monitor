#!/usr/bin/env zsh
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
source "$PROJECT_ROOT/scripts/lib/load_runtime_config.sh"
BUILD_DIR="${BUILD_DIR:-$PROJECT_ROOT/build/Qt_6_10_1_for_macOS-Debug}"
RUNTIME_DIR="${RUNTIME_DIR:-/tmp/cmm_sim_runtime}"
BACKEND_URL="${BACKEND_URL:-http://127.0.0.1:8080}"

echo "[process]"
pgrep -af 'sim_backend|sim_admin_qml|sim_classroom_qml' || true

echo
echo "[port]"
lsof -nP -iTCP:8080 -sTCP:LISTEN || true

echo
echo "[backend health]"
ok=0
for _ in {1..8}; do
  if curl -sS -m 2 "$BACKEND_URL/health"; then
    ok=1
    break
  fi
  sleep 0.3
done
if [[ "$ok" -ne 1 ]]; then
  echo "backend unreachable at $BACKEND_URL"
fi
echo

echo
echo "[logs]"
for f in "$RUNTIME_DIR/backend.log" "$RUNTIME_DIR/admin.log" "$RUNTIME_DIR/classroom.log"; do
  if [[ -f "$f" ]]; then
    echo "-- $f --"
    tail -n 8 "$f"
  fi
done
