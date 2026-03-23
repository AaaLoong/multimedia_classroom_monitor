#!/usr/bin/env zsh

if [[ -n "${CMM_RUNTIME_CONFIG_LOADED:-}" ]]; then
  return 0
fi
CMM_RUNTIME_CONFIG_LOADED=1

if [[ -z "${PROJECT_ROOT:-}" ]]; then
  PROJECT_ROOT="$(cd "$(dirname "${(%):-%x}")/../.." && pwd)"
fi

CONFIG_DIR="${CONFIG_DIR:-$PROJECT_ROOT/config}"
RUNTIME_ENV_FILE="${RUNTIME_ENV_FILE:-$CONFIG_DIR/runtime.env}"
RUNTIME_ENV_LOCAL_FILE="${RUNTIME_ENV_LOCAL_FILE:-$CONFIG_DIR/runtime.env.local}"

load_runtime_env_file() {
  local env_file="$1"
  if [[ -f "$env_file" ]]; then
    set -a
    source "$env_file"
    set +a
  fi
}

load_runtime_env_file "$RUNTIME_ENV_FILE"
load_runtime_env_file "$RUNTIME_ENV_LOCAL_FILE"
