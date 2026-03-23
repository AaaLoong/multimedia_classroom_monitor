#!/usr/bin/env zsh
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
source "$PROJECT_ROOT/scripts/lib/load_runtime_config.sh"

"$PROJECT_ROOT/scripts/start_backend.sh"
"$PROJECT_ROOT/scripts/start_clients.sh"
"$PROJECT_ROOT/scripts/start_device.sh"
"$PROJECT_ROOT/scripts/status.sh"
