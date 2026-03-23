#!/usr/bin/env zsh
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$PROJECT_ROOT/build/Qt_6_10_1_for_macOS-Debug}"
RUNTIME_DIR="${RUNTIME_DIR:-/tmp/cmm_sim_runtime}"

for pid_file in "$RUNTIME_DIR/backend.pid" "$RUNTIME_DIR/admin.pid" "$RUNTIME_DIR/classroom.pid" "$RUNTIME_DIR/device.pid"; do
  if [[ -f "$pid_file" ]]; then
    pid="$(cat "$pid_file" 2>/dev/null || true)"
    if [[ -n "$pid" ]]; then
      kill "$pid" 2>/dev/null || true
    fi
    rm -f "$pid_file"
  fi
done

pkill -f "$BUILD_DIR/sim_backend" 2>/dev/null || true
pkill -x sim_backend 2>/dev/null || true
pkill -f "$BUILD_DIR/sim_admin_qml" 2>/dev/null || true
pkill -f "$BUILD_DIR/sim_classroom_qml" 2>/dev/null || true
pkill -f "$BUILD_DIR/sim_device_client" 2>/dev/null || true

echo "all processes stopped"
