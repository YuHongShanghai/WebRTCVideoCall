#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEPLOY_DIR="$ROOT_DIR/deploy"
COTURN_DIR="$DEPLOY_DIR/coturn"
ENV_FILE="$COTURN_DIR/.env.local"
CONF_FILE="$COTURN_DIR/turnserver.local.conf"

detect_ip() {
  local ip
  ip="$(ipconfig getifaddr en0 2>/dev/null || true)"
  if [[ -z "$ip" ]]; then
    ip="$(ipconfig getifaddr en1 2>/dev/null || true)"
  fi
  if [[ -z "$ip" ]]; then
    ip="$(hostname -I 2>/dev/null | awk '{print $1}' || true)"
  fi
  if [[ -z "$ip" ]]; then
    ip="127.0.0.1"
  fi
  printf '%s\n' "$ip"
}

generate_secret() {
  if command -v openssl >/dev/null 2>&1; then
    openssl rand -hex 32
  else
    date +%s | shasum -a 256 | awk '{print $1}'
  fi
}

TURN_HOST="${1:-$(detect_ip)}"
TURN_REALM="${TURN_REALM:-$TURN_HOST}"
TURN_SECRET="${TURN_SECRET:-$(generate_secret)}"
TURN_EXTERNAL_IP="${TURN_EXTERNAL_IP:-$TURN_HOST}"

mkdir -p "$COTURN_DIR"

cat > "$ENV_FILE" <<EOF
WEBRTC_TURN_HOST=$TURN_HOST
WEBRTC_TURN_REALM=$TURN_REALM
WEBRTC_TURN_SECRET=$TURN_SECRET
WEBRTC_TURN_TTL_SECONDS=86400
WEBRTC_STUN_URL=stun:$TURN_HOST:3478
WEBRTC_TURNS_ENABLED=0
TURN_EXTERNAL_IP=$TURN_EXTERNAL_IP
EOF

cat > "$CONF_FILE" <<EOF
listening-port=3478
tls-listening-port=5349

fingerprint
lt-cred-mech
use-auth-secret
static-auth-secret=$TURN_SECRET

realm=$TURN_REALM
server-name=$TURN_REALM
external-ip=$TURN_EXTERNAL_IP

min-port=49152
max-port=49200

no-multicast-peers
stale-nonce

log-file=stdout
verbose
EOF

printf 'Generated %s\n' "$ENV_FILE"
printf 'Generated %s\n' "$CONF_FILE"
printf 'TURN host: %s\n' "$TURN_HOST"
