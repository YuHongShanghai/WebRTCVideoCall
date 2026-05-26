#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ENV_FILE="$ROOT_DIR/deploy/coturn/.env.local"
CONF_FILE="$ROOT_DIR/deploy/coturn/turnserver.local.conf"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/cmake-build-debug}"

if [[ ! -f "$ENV_FILE" || ! -f "$CONF_FILE" ]]; then
  "$ROOT_DIR/scripts/setup-turn-local.sh" "${1:-}"
fi

set -a
# shellcheck disable=SC1090
source "$ENV_FILE"
set +a

cmake --build "$BUILD_DIR" --target WebRTCClientServer -j8

if docker compose version >/dev/null 2>&1; then
  COMPOSE_CMD=(docker compose)
elif command -v docker-compose >/dev/null 2>&1; then
  COMPOSE_CMD=(docker-compose)
else
  COMPOSE_CMD=()
fi

if [[ ${#COMPOSE_CMD[@]} -gt 0 ]]; then
  if ! "${COMPOSE_CMD[@]}" \
      -f "$ROOT_DIR/deploy/docker-compose.yml" \
      --env-file "$ENV_FILE" \
      up -d --force-recreate; then
    echo "coturn container did not start. Check Docker and Compose installation." >&2
    echo "Continuing with signaling server startup; TURN fallback needs coturn running." >&2
  fi
else
  echo "Docker Compose not found; skip coturn container startup" >&2
fi

echo "Starting signaling server with TURN host: $WEBRTC_TURN_HOST"
export WEBRTC_SIGNAL_HOST="${WEBRTC_SIGNAL_HOST:-0.0.0.0}"
export WEBRTC_SIGNAL_PORT="${WEBRTC_SIGNAL_PORT:-8000}"

if [[ -x "$BUILD_DIR/WebRTCClientServer/WebRTCClientServer" ]]; then
  exec "$BUILD_DIR/WebRTCClientServer/WebRTCClientServer"
fi

exec "$BUILD_DIR/WebRTCClientServer"
