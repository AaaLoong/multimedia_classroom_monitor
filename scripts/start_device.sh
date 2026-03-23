#!/usr/bin/env zsh
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
source "$PROJECT_ROOT/scripts/lib/load_runtime_config.sh"
BUILD_DIR="${BUILD_DIR:-$PROJECT_ROOT/build/Qt_6_10_1_for_macOS-Debug}"
DEVICE_BIN="$BUILD_DIR/sim_device_client"

ROOM_ID="${ROOM_ID:-A101}"
MQTT_URL="${MQTT_URL:-${SIM_MQTT_URL:-tcp://127.0.0.1:1883}}"
SIM_DEVICE_HEADLESS="${SIM_DEVICE_HEADLESS:-0}"
RUNTIME_DIR="${RUNTIME_DIR:-/tmp/cmm_sim_runtime}"
mkdir -p "$RUNTIME_DIR"
PID_FILE="$RUNTIME_DIR/device.pid"
LOG_FILE="$RUNTIME_DIR/device.log"

if [[ ! -x "$DEVICE_BIN" ]]; then
  echo "ERROR: device simulator binary not found: $DEVICE_BIN"
  exit 1
fi

if [[ "$MQTT_URL" =~ '^tcp://([^:/]+):([0-9]+)$' ]]; then
  mqtt_host="${match[1]}"
  mqtt_port="${match[2]}"
  if ! nc -z "$mqtt_host" "$mqtt_port" >/dev/null 2>&1; then
    echo "ERROR: MQTT broker not reachable at $mqtt_host:$mqtt_port"
    exit 1
  fi
fi

if [[ -f "$PID_FILE" ]]; then
  old_pid="$(cat "$PID_FILE" 2>/dev/null || true)"
  if [[ -n "$old_pid" ]] && kill -0 "$old_pid" 2>/dev/null; then
    echo "device simulator already running, pid=$old_pid"
    exit 0
  fi
fi

pkill -f "$DEVICE_BIN" 2>/dev/null || true

launch_args=("$ROOM_ID" "$MQTT_URL")
if [[ "$SIM_DEVICE_HEADLESS" == "1" ]]; then
  launch_args=(--headless "${launch_args[@]}")
fi

nohup "$DEVICE_BIN" "${launch_args[@]}" >"$LOG_FILE" 2>&1 < /dev/null &
new_pid=$!
echo "$new_pid" > "$PID_FILE"

echo "device simulator started: pid=$new_pid room=$ROOM_ID mqtt=$MQTT_URL headless=$SIM_DEVICE_HEADLESS"
echo "log: $LOG_FILE"
