#!/usr/bin/env bash
# ipc_mini cloud: one command for coturn + signaling.
#
# signaling 源码挂在容器里，远程已有 node_modules，改 JS/页面只需重启。
# 只有改了 package.json / Dockerfile 才需要 rebuild。
#
# On the ECS:
#   cd /path/to/ipc-mini/cloud
#   ./up.sh              # 重启 signaling + coturn（日常更新）
#   ./up.sh signaling    # 只重启信令
#   ./up.sh coturn       # 只重启 coturn（改了 turnserver.conf）
#   ./up.sh rebuild      # 依赖或镜像变了才用
#   ./up.sh down
#   ./up.sh logs
#   ./up.sh status
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
COMPOSE_DIR="$ROOT/coturn"

compose() {
  if ! command -v docker >/dev/null 2>&1; then
    echo "docker not found" >&2
    exit 1
  fi
  cd "$COMPOSE_DIR"
  if docker compose version >/dev/null 2>&1; then
    docker compose "$@"
  elif command -v docker-compose >/dev/null 2>&1; then
    docker-compose "$@"
  else
    echo "docker compose not found" >&2
    exit 1
  fi
}

usage() {
  sed -n '2,16p' "$0" | sed 's/^# \{0,1\}//'
}

cmd="${1:-all}"
case "$cmd" in
  all|up|restart)
    echo "[ipc_mini] restart signaling + coturn (no rebuild)"
    compose up -d
    compose restart
    compose ps
    ;;
  signaling)
    echo "[ipc_mini] restart signaling (bind-mount, keep node_modules)"
    compose up -d signaling
    compose restart signaling
    compose ps signaling
    ;;
  coturn)
    echo "[ipc_mini] restart coturn (reload turnserver.conf)"
    compose up -d coturn
    compose restart coturn
    compose ps coturn
    ;;
  rebuild)
    echo "[ipc_mini] rebuild signaling image (package.json / Dockerfile)"
    compose up -d --build --force-recreate signaling
    compose ps
    ;;
  down)
    compose down
    ;;
  logs)
    if [ -n "${2:-}" ]; then
      compose logs -f --tail=80 "$2"
    else
      compose logs -f --tail=80
    fi
    ;;
  status|ps)
    compose ps
    ;;
  -h|--help|help)
    usage
    ;;
  *)
    echo "unknown command: $cmd" >&2
    usage >&2
    exit 1
    ;;
esac
