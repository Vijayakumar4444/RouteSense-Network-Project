#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
trace="$root/results/netanim/routesense-dynamic-cross.xml"
if [[ $# -gt 0 ]]; then trace="$1"; fi
if [[ ! -f "$trace" ]]; then echo "Trace not found: $trace" >&2; exit 1; fi
binary="$root/build/netanim-presentation/app/netanim"
if [[ ! -x "$binary" ]]; then
  echo "Build first: python3 tools/netanim-presentation/prepare.py --source /tmp/netanim" >&2
  exit 1
fi
exec "$binary" "$(realpath "$trace")"
