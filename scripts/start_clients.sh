#!/usr/bin/env zsh
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
source "$PROJECT_ROOT/scripts/lib/load_runtime_config.sh"
BUILD_DIR="${BUILD_DIR:-$PROJECT_ROOT/build/Qt_6_10_1_for_macOS-Debug}"

ADMIN_BIN="$BUILD_DIR/sim_admin_qml"
CLASSROOM_BIN="$BUILD_DIR/sim_classroom_qml"

BACKEND_URL="${BACKEND_URL:-http://127.0.0.1:8080}"
ROOM_ID="${ROOM_ID:-A101}"

RUNTIME_DIR="${RUNTIME_DIR:-/tmp/cmm_sim_runtime}"
mkdir -p "$RUNTIME_DIR"

if [[ ! -x "$ADMIN_BIN" ]]; then
  echo "ERROR: admin binary not found: $ADMIN_BIN"
  exit 1
fi

if [[ ! -x "$CLASSROOM_BIN" ]]; then
  echo "ERROR: classroom binary not found: $CLASSROOM_BIN"
  exit 1
fi

if ! curl -sS -m 2 "$BACKEND_URL/health" >/dev/null 2>&1; then
  echo "backend unreachable at $BACKEND_URL, trying to start backend..."
  "$PROJECT_ROOT/scripts/start_backend.sh"
fi

pkill -f "$ADMIN_BIN" 2>/dev/null || true
pkill -f "$CLASSROOM_BIN" 2>/dev/null || true

nohup "$ADMIN_BIN" "$BACKEND_URL" >"$RUNTIME_DIR/admin.log" 2>&1 < /dev/null &
echo $! > "$RUNTIME_DIR/admin.pid"

nohup "$CLASSROOM_BIN" "$ROOM_ID" "$BACKEND_URL" >"$RUNTIME_DIR/classroom.log" 2>&1 < /dev/null &
echo $! > "$RUNTIME_DIR/classroom.pid"

echo "clients started: backend=$BACKEND_URL room=$ROOM_ID"
