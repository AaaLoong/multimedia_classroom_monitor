#!/usr/bin/env zsh
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$PROJECT_ROOT/build/Qt_6_10_1_for_macOS-Debug}"
SIMULATOR_BIN="$BUILD_DIR/sim_device_client"

ROOM_ID="${ROOM_ID:-A101}"
MQTT_URL="${MQTT_URL:-tcp://127.0.0.1:1883}"
RUNTIME_DIR="${RUNTIME_DIR:-/tmp/cmm_sim_runtime}"
mkdir -p "$RUNTIME_DIR"

if [[ ! -x "$SIMULATOR_BIN" ]]; then
  echo "ERROR: simulator binary not found: $SIMULATOR_BIN"
  exit 1
fi

pkill -f "$SIMULATOR_BIN" 2>/dev/null || true
nohup "$SIMULATOR_BIN" "$ROOM_ID" "$MQTT_URL" >"$RUNTIME_DIR/simulator.log" 2>&1 < /dev/null &
echo $! > "$RUNTIME_DIR/simulator.pid"

echo "simulator started: room=$ROOM_ID mqtt=$MQTT_URL"
