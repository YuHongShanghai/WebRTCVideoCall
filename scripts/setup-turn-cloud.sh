#!/usr/bin/env bash
set -euo pipefail

PUBLIC_IP="${1:-124.222.143.151}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

TURN_EXTERNAL_IP="$PUBLIC_IP" "$SCRIPT_DIR/setup-turn-local.sh" "$PUBLIC_IP"
