#!/usr/bin/env zsh
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
source "$PROJECT_ROOT/scripts/lib/load_runtime_config.sh"
BUILD_DIR="${BUILD_DIR:-$PROJECT_ROOT/build/Qt_6_10_1_for_macOS-Debug}"
BACKEND_BIN="$BUILD_DIR/sim_backend"

RUNTIME_DIR="${RUNTIME_DIR:-/tmp/cmm_sim_runtime}"
mkdir -p "$RUNTIME_DIR"
PID_FILE="$RUNTIME_DIR/backend.pid"
LOG_FILE="$RUNTIME_DIR/backend.log"

SIM_DB_DRIVER="${SIM_DB_DRIVER:-QMYSQL}"
SIM_DB_HOST="${SIM_DB_HOST:-127.0.0.1}"
SIM_DB_PORT="${SIM_DB_PORT:-3307}"
SIM_DB_NAME="${SIM_DB_NAME:-cmm_sim}"
SIM_DB_USER="${SIM_DB_USER:-root}"
SIM_DB_PASS="${SIM_DB_PASS:-}"
SIM_MQTT_URL="${SIM_MQTT_URL:-tcp://127.0.0.1:1883}"

BACKEND_URL="${BACKEND_URL:-http://127.0.0.1:8080}"
HEALTH_PATH="/health"

QT_PLUGIN_PATH_DEFAULT="/Users/achang/Qt/6.10.1/macos/plugins"
if [[ -z "${QT_PLUGIN_PATH:-}" ]]; then
  export QT_PLUGIN_PATH="$QT_PLUGIN_PATH_DEFAULT"
fi

QMYSQL_PLUGIN="$QT_PLUGIN_PATH/sqldrivers/libqsqlmysql.dylib"

if [[ ! -x "$BACKEND_BIN" ]]; then
  echo "ERROR: backend binary not found: $BACKEND_BIN"
  exit 1
fi

if [[ "$SIM_DB_DRIVER" == "QMYSQL" ]] && [[ ! -f "$QMYSQL_PLUGIN" ]]; then
  echo "ERROR: QMYSQL plugin missing: $QMYSQL_PLUGIN"
  exit 1
fi

if [[ "$SIM_DB_DRIVER" == "QMYSQL" ]]; then
  mysql_client_lib="$(otool -L "$QMYSQL_PLUGIN" | awk '/libmysqlclient\\./ {print $1; exit}')"
  if [[ -n "$mysql_client_lib" ]] && [[ ! -e "$mysql_client_lib" ]]; then
    echo "ERROR: QMYSQL dependency missing: $mysql_client_lib"
    echo "hint: relink libmysqlclient or rebuild QMYSQL plugin"
    exit 1
  fi
fi

db_reachable=0
for _ in {1..20}; do
  if nc -z "$SIM_DB_HOST" "$SIM_DB_PORT" >/dev/null 2>&1; then
    db_reachable=1
    break
  fi
  sleep 0.2
done
if [[ "$db_reachable" -ne 1 ]]; then
  echo "ERROR: MySQL not reachable at $SIM_DB_HOST:$SIM_DB_PORT"
  exit 1
fi

if command -v mysql >/dev/null 2>&1; then
  mysql_probe_status=0
  mysql_probe_output="$(
    MYSQL_PWD="$SIM_DB_PASS" mysql -h "$SIM_DB_HOST" -P "$SIM_DB_PORT" -u "$SIM_DB_USER" -D "$SIM_DB_NAME" -e "SELECT 1" 2>&1
  )" || mysql_probe_status=$?
  if [[ "$mysql_probe_status" -ne 0 ]]; then
    if [[ -z "$SIM_DB_PASS" ]]; then
      echo "ERROR: MySQL authentication failed and SIM_DB_PASS is empty"
      echo "hint: set SIM_DB_PASS in $PROJECT_ROOT/config/runtime.env"
    else
      echo "ERROR: MySQL credentials rejected for $SIM_DB_USER@$SIM_DB_HOST:$SIM_DB_PORT/$SIM_DB_NAME"
    fi
    if [[ -n "$mysql_probe_output" ]]; then
      echo "detail: ${mysql_probe_output##*$'\n'}"
    fi
    exit 1
  fi
fi

if [[ "$SIM_MQTT_URL" =~ '^tcp://([^:/]+):([0-9]+)$' ]]; then
  mqtt_host="${match[1]}"
  mqtt_port="${match[2]}"
  mqtt_reachable=0
  for _ in {1..20}; do
    if nc -z "$mqtt_host" "$mqtt_port" >/dev/null 2>&1; then
      mqtt_reachable=1
      break
    fi
    sleep 0.2
  done
  if [[ "$mqtt_reachable" -ne 1 ]]; then
    echo "ERROR: MQTT broker not reachable at $mqtt_host:$mqtt_port"
    exit 1
  fi
fi

if [[ -f "$PID_FILE" ]]; then
  old_pid="$(cat "$PID_FILE" 2>/dev/null || true)"
  if [[ -n "$old_pid" ]] && kill -0 "$old_pid" 2>/dev/null; then
    echo "backend already running, pid=$old_pid"
    exit 0
  fi
fi

# Ensure only one backend instance; cpp-httplib enables SO_REUSEPORT on macOS, so
# stale processes can silently share the same port and produce inconsistent results.
pkill -x sim_backend 2>/dev/null || true
sleep 0.4

if [[ -z "${SIM_ADMIN_INIT_USERNAME:-}" && -z "${SIM_ADMIN_INIT_PASSWORD:-}" ]] && command -v mysql >/dev/null 2>&1; then
  table_exists="$(MYSQL_PWD="$SIM_DB_PASS" mysql -N -s -h "$SIM_DB_HOST" -P "$SIM_DB_PORT" -u "$SIM_DB_USER" -D "$SIM_DB_NAME" \
    -e "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema='${SIM_DB_NAME}' AND table_name='admin_users';" 2>/dev/null || echo "0")"
  admin_count="0"
  if [[ "$table_exists" == "1" ]]; then
    admin_count="$(MYSQL_PWD="$SIM_DB_PASS" mysql -N -s -h "$SIM_DB_HOST" -P "$SIM_DB_PORT" -u "$SIM_DB_USER" -D "$SIM_DB_NAME" \
      -e "SELECT COUNT(*) FROM admin_users;" 2>/dev/null || echo "0")"
  fi

  if [[ "$admin_count" == "0" ]]; then
    SIM_ADMIN_INIT_USERNAME="admin"
    SIM_ADMIN_INIT_PASSWORD="$(openssl rand -hex 6)"
    export SIM_ADMIN_INIT_USERNAME SIM_ADMIN_INIT_PASSWORD
    echo "bootstrap admin generated for first run:"
    echo "  username=$SIM_ADMIN_INIT_USERNAME"
    echo "  password=$SIM_ADMIN_INIT_PASSWORD"
  fi
elif [[ -n "${SIM_ADMIN_INIT_USERNAME:-}" && -z "${SIM_ADMIN_INIT_PASSWORD:-}" ]] || [[ -z "${SIM_ADMIN_INIT_USERNAME:-}" && -n "${SIM_ADMIN_INIT_PASSWORD:-}" ]]; then
  echo "ERROR: SIM_ADMIN_INIT_USERNAME and SIM_ADMIN_INIT_PASSWORD must be set together"
  exit 1
fi

: > "$LOG_FILE"
export SIM_DB_DRIVER SIM_DB_HOST SIM_DB_PORT SIM_DB_NAME SIM_DB_USER SIM_DB_PASS SIM_MQTT_URL
export SIM_ADMIN_INIT_USERNAME="${SIM_ADMIN_INIT_USERNAME:-}"
export SIM_ADMIN_INIT_PASSWORD="${SIM_ADMIN_INIT_PASSWORD:-}"
nohup "$BACKEND_BIN" >"$LOG_FILE" 2>&1 < /dev/null &
new_pid=$!
echo "$new_pid" > "$PID_FILE"

ok=0
for _ in {1..30}; do
  if curl -sS -m 1 "$BACKEND_URL$HEALTH_PATH" 2>/dev/null | rg -q '"status":"ok"'; then
    ok=1
    break
  fi
  sleep 0.3
done

if [[ "$ok" -ne 1 ]]; then
  echo "ERROR: backend failed health check, pid=$new_pid"
  tail -n 80 "$LOG_FILE" || true
  exit 1
fi

echo "backend started: pid=$new_pid"
echo "log: $LOG_FILE"
